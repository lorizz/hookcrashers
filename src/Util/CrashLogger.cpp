#include "CrashLogger.h"

#include <windows.h>
#include <dbghelp.h>

#include <array>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace HookCrashers::Util {
    namespace {
        std::string g_logDirectory;
        PVOID g_vectoredHandler = nullptr;
        LPTOP_LEVEL_EXCEPTION_FILTER g_previousUnhandledFilter = nullptr;
        volatile LONG g_loggedCrash = 0;

        const char* ExceptionCodeName(DWORD code) {
            switch (code) {
            case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
            case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
            case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT";
            case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
            case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
            case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
            case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
            case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
            case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
            case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
            case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION";
            case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
            case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
            case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP";
            case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
            default: return "UNKNOWN_EXCEPTION";
            }
        }

        std::string Hex(uint64_t value, int width = 0) {
            std::ostringstream ss;
            ss << "0x" << std::uppercase << std::hex << std::setfill('0');
            if (width > 0) {
                ss << std::setw(width);
            }
            ss << value;
            return ss.str();
        }

        std::string ModuleNameFromAddress(DWORD64 address, DWORD64* moduleBaseOut = nullptr) {
            HMODULE module = nullptr;
            char path[MAX_PATH] = {};
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(address)), &module) && module) {
                if (moduleBaseOut) {
                    *moduleBaseOut = reinterpret_cast<DWORD64>(module);
                }
                if (GetModuleFileNameA(module, path, MAX_PATH)) {
                    std::string full(path);
                    const size_t slash = full.find_last_of("\\/");
                    return slash == std::string::npos ? full : full.substr(slash + 1);
                }
            }
            if (moduleBaseOut) {
                *moduleBaseOut = 0;
            }
            return "<unknown>";
        }

        void WriteTimestamp(FILE* file) {
            std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            char buffer[64] = {};
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
            std::fprintf(file, "Timestamp: %s\n", buffer);
        }

        void WriteRegisters(FILE* file, const CONTEXT& ctx) {
#if defined(_M_IX86)
            std::fprintf(file, "Registers:\n");
            std::fprintf(file, "  EAX=%s EBX=%s ECX=%s EDX=%s\n", Hex(ctx.Eax, 8).c_str(), Hex(ctx.Ebx, 8).c_str(), Hex(ctx.Ecx, 8).c_str(), Hex(ctx.Edx, 8).c_str());
            std::fprintf(file, "  ESI=%s EDI=%s EBP=%s ESP=%s\n", Hex(ctx.Esi, 8).c_str(), Hex(ctx.Edi, 8).c_str(), Hex(ctx.Ebp, 8).c_str(), Hex(ctx.Esp, 8).c_str());
            std::fprintf(file, "  EIP=%s EFLAGS=%s\n", Hex(ctx.Eip, 8).c_str(), Hex(ctx.EFlags, 8).c_str());
#else
            std::fprintf(file, "Registers:\n");
            std::fprintf(file, "  RIP=%s RSP=%s RBP=%s\n", Hex(ctx.Rip, 16).c_str(), Hex(ctx.Rsp, 16).c_str(), Hex(ctx.Rbp, 16).c_str());
            std::fprintf(file, "  RAX=%s RBX=%s RCX=%s RDX=%s\n", Hex(ctx.Rax, 16).c_str(), Hex(ctx.Rbx, 16).c_str(), Hex(ctx.Rcx, 16).c_str(), Hex(ctx.Rdx, 16).c_str());
            std::fprintf(file, "  RSI=%s RDI=%s R8=%s R9=%s\n", Hex(ctx.Rsi, 16).c_str(), Hex(ctx.Rdi, 16).c_str(), Hex(ctx.R8, 16).c_str(), Hex(ctx.R9, 16).c_str());
#endif
        }

        void WriteSymbol(FILE* file, HANDLE process, DWORD64 address) {
            DWORD64 moduleBase = 0;
            const std::string moduleName = ModuleNameFromAddress(address, &moduleBase);
            const DWORD64 rva = moduleBase ? address - moduleBase : 0;

            std::array<unsigned char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbolBuffer{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer.data());
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;
            const BOOL hasSymbol = SymFromAddr(process, address, &displacement, symbol);

            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            const BOOL hasLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line);

            std::fprintf(file, "%s %s!+%s",
                Hex(address, sizeof(void*) == 4 ? 8 : 16).c_str(),
                moduleName.c_str(),
                Hex(rva, 0).c_str());
            if (hasSymbol) {
                std::fprintf(file, " %s+%llu", symbol->Name, static_cast<unsigned long long>(displacement));
            }
            if (hasLine) {
                std::fprintf(file, " (%s:%lu+%lu)", line.FileName, line.LineNumber, lineDisplacement);
            }
            std::fprintf(file, "\n");
        }

        void WriteStack(FILE* file, CONTEXT context) {
            HANDLE process = GetCurrentProcess();
            HANDLE thread = GetCurrentThread();

            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
            SymInitialize(process, nullptr, TRUE);

            STACKFRAME64 frame{};
            DWORD machine = 0;
#if defined(_M_IX86)
            machine = IMAGE_FILE_MACHINE_I386;
            frame.AddrPC.Offset = context.Eip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context.Ebp;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context.Esp;
            frame.AddrStack.Mode = AddrModeFlat;
#else
            machine = IMAGE_FILE_MACHINE_AMD64;
            frame.AddrPC.Offset = context.Rip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context.Rbp;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context.Rsp;
            frame.AddrStack.Mode = AddrModeFlat;
#endif

            std::fprintf(file, "Callstack:\n");
            for (int i = 0; i < 64; ++i) {
                if (frame.AddrPC.Offset == 0) {
                    break;
                }
                std::fprintf(file, "  #%02d ", i);
                WriteSymbol(file, process, frame.AddrPC.Offset);
                if (!StackWalk64(machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                    break;
                }
            }
            SymCleanup(process);
        }

        void WriteCrashReport(EXCEPTION_POINTERS* info, const char* source) {
            if (!info || !info->ExceptionRecord || !info->ContextRecord) {
                return;
            }
            if (InterlockedExchange(&g_loggedCrash, 1) != 0) {
                return;
            }

            std::string path = g_logDirectory.empty() ? "HookCrashersCrash.log" : (g_logDirectory + "HookCrashersCrash.log");
            FILE* file = nullptr;
            fopen_s(&file, path.c_str(), "ab");
            if (!file) {
                return;
            }

            const EXCEPTION_RECORD* rec = info->ExceptionRecord;
            DWORD64 moduleBase = 0;
            const std::string moduleName = ModuleNameFromAddress(reinterpret_cast<DWORD64>(rec->ExceptionAddress), &moduleBase);
            const DWORD64 rva = moduleBase ? reinterpret_cast<DWORD64>(rec->ExceptionAddress) - moduleBase : 0;

            std::fprintf(file, "\n================ HookCrashers Crash Report ================\n");
            WriteTimestamp(file);
            std::fprintf(file, "Source: %s\n", source ? source : "unknown");
            std::fprintf(file, "ThreadId: %lu\n", GetCurrentThreadId());
            std::fprintf(file, "Exception: %s (%s)\n", ExceptionCodeName(rec->ExceptionCode), Hex(rec->ExceptionCode, 8).c_str());
            std::fprintf(file, "Address: %s %s!+%s\n", Hex(reinterpret_cast<DWORD64>(rec->ExceptionAddress), sizeof(void*) == 4 ? 8 : 16).c_str(), moduleName.c_str(), Hex(rva).c_str());
            if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
                const char* op = rec->ExceptionInformation[0] == 0 ? "read" : (rec->ExceptionInformation[0] == 1 ? "write" : "execute");
                std::fprintf(file, "AccessViolation: %s at %s\n", op, Hex(rec->ExceptionInformation[1], sizeof(void*) == 4 ? 8 : 16).c_str());
            }
            std::fprintf(file, "ExceptionFlags: %s Parameters: %lu\n", Hex(rec->ExceptionFlags, 8).c_str(), rec->NumberParameters);
            for (DWORD i = 0; i < rec->NumberParameters; ++i) {
                std::fprintf(file, "  Param[%lu]=%s\n", i, Hex(rec->ExceptionInformation[i], sizeof(void*) == 4 ? 8 : 16).c_str());
            }
            WriteRegisters(file, *info->ContextRecord);
            WriteStack(file, *info->ContextRecord);
            std::fprintf(file, "===========================================================\n");
            std::fflush(file);
            fclose(file);
        }

        bool IsIgnoredFirstChanceException(DWORD code) {
            // OutputDebugString raises these continuable exceptions for attached debuggers/listeners.
            // They are not crashes; Steam emits them during startup.
            constexpr DWORD kDbgPrintException = 0x40010006;
            constexpr DWORD kDbgPrintExceptionWide = 0x4001000A;
            constexpr DWORD kMsvcThreadNameException = 0x406D1388;
            return code == EXCEPTION_BREAKPOINT
                || code == EXCEPTION_SINGLE_STEP
                || code == kDbgPrintException
                || code == kDbgPrintExceptionWide
                || code == kMsvcThreadNameException;
        }

        LONG WINAPI VectoredExceptionHandler(EXCEPTION_POINTERS* info) {
            if (info && info->ExceptionRecord) {
                const DWORD code = info->ExceptionRecord->ExceptionCode;
                if (!IsIgnoredFirstChanceException(code)) {
                    WriteCrashReport(info, "VectoredExceptionHandler");
                }
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }

        LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* info) {
            WriteCrashReport(info, "UnhandledExceptionFilter");
            if (g_previousUnhandledFilter) {
                return g_previousUnhandledFilter(info);
            }
            return EXCEPTION_EXECUTE_HANDLER;
        }
    }

    void CrashLogger::Install(const std::string& logDirectory) {
        g_logDirectory = logDirectory;
        InterlockedExchange(&g_loggedCrash, 0);
        if (!g_vectoredHandler) {
            g_vectoredHandler = AddVectoredExceptionHandler(1, VectoredExceptionHandler);
        }
        g_previousUnhandledFilter = SetUnhandledExceptionFilter(UnhandledExceptionHandler);
    }

    void CrashLogger::Uninstall() {
        if (g_vectoredHandler) {
            RemoveVectoredExceptionHandler(g_vectoredHandler);
            g_vectoredHandler = nullptr;
        }
        SetUnhandledExceptionFilter(g_previousUnhandledFilter);
        g_previousUnhandledFilter = nullptr;
    }
}