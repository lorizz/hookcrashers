#include "CrashHandler.h"

#include "Logger.h"
#include "Version.h"

#include <windows.h>
#include <dbghelp.h>

#include <array>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace HookCrashers::Core::CrashHandler {
namespace {

std::string g_Directory;
PVOID g_VectoredHandler = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_PreviousUnhandledFilter = nullptr;
volatile LONG g_LoggedCrash = 0;

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

std::string Hex(unsigned long long value, int width = 0) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setfill('0');
    if (width > 0) {
        stream << std::setw(width);
    }
    stream << value;
    return stream.str();
}

std::string JoinPath(const std::string& directory, const char* filename) {
    if (directory.empty()) {
        return filename;
    }
    if (directory.back() == '\\' || directory.back() == '/') {
        return directory + filename;
    }
    return directory + "\\" + filename;
}

std::string ModuleNameFromAddress(DWORD64 address, DWORD64* moduleBaseOut = nullptr) {
    HMODULE module = nullptr;
    char path[MAX_PATH] {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(address)),
                           &module) &&
        module) {
        if (moduleBaseOut) {
            *moduleBaseOut = reinterpret_cast<DWORD64>(module);
        }
        if (GetModuleFileNameA(module, path, sizeof(path))) {
            std::string full(path);
            const std::size_t slash = full.find_last_of("\\/");
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
    std::tm local {};
    localtime_s(&local, &now);
    char buffer[64] {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    std::fprintf(file, "Timestamp: %s\n", buffer);
}

void WriteRegisters(FILE* file, const CONTEXT& context) {
#if defined(_M_IX86)
    std::fprintf(file, "Registers:\n");
    std::fprintf(file, "  EAX=%s EBX=%s ECX=%s EDX=%s\n",
                 Hex(context.Eax, 8).c_str(),
                 Hex(context.Ebx, 8).c_str(),
                 Hex(context.Ecx, 8).c_str(),
                 Hex(context.Edx, 8).c_str());
    std::fprintf(file, "  ESI=%s EDI=%s EBP=%s ESP=%s\n",
                 Hex(context.Esi, 8).c_str(),
                 Hex(context.Edi, 8).c_str(),
                 Hex(context.Ebp, 8).c_str(),
                 Hex(context.Esp, 8).c_str());
    std::fprintf(file, "  EIP=%s EFLAGS=%s\n", Hex(context.Eip, 8).c_str(), Hex(context.EFlags, 8).c_str());
#else
    std::fprintf(file, "Registers:\n");
    std::fprintf(file, "  RIP=%s RSP=%s RBP=%s\n",
                 Hex(context.Rip, 16).c_str(),
                 Hex(context.Rsp, 16).c_str(),
                 Hex(context.Rbp, 16).c_str());
    std::fprintf(file, "  RAX=%s RBX=%s RCX=%s RDX=%s\n",
                 Hex(context.Rax, 16).c_str(),
                 Hex(context.Rbx, 16).c_str(),
                 Hex(context.Rcx, 16).c_str(),
                 Hex(context.Rdx, 16).c_str());
#endif
}

// ReadProcessMemory reports unreadable addresses without raising another local
// exception inside the first-chance crash handler. Keep all samples bounded.
void WriteMemoryWords(FILE* file, const char* label, std::uintptr_t address, unsigned count) {
    std::fprintf(file, "%s @ %s:\n", label, Hex(address, sizeof(void*) * 2).c_str());
    for (unsigned i = 0; i < count; ++i) {
        const auto offset = static_cast<std::uintptr_t>(i) * sizeof(std::uint32_t);
        std::uint32_t word = 0;
        SIZE_T bytes = 0;
        if (address > UINTPTR_MAX - offset ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address + offset),
                               &word, sizeof(word), &bytes) || bytes != sizeof(word)) {
            std::fprintf(file, "  +%02X <unreadable>\n", static_cast<unsigned>(offset));
            break;
        }
        std::fprintf(file, "  +%02X %08X\n", static_cast<unsigned>(offset), word);
    }
}

void WriteNativeMemoryContext(FILE* file, const CONTEXT& context) {
#if defined(_M_IX86)
    // SetMember uses ECX=receiver, EDX=vtable, EAX=name ID and ESI/EDI/EBX
    // for the VM operand slots. These are raw samples, not validated objects.
    std::fprintf(file, "Native memory context (raw words, not validated AVM objects):\n");
    WriteMemoryWords(file, "ECX", context.Ecx, 8);
    WriteMemoryWords(file, "EDX", context.Edx, 8);
    WriteMemoryWords(file, "ESI", context.Esi, 4);
    WriteMemoryWords(file, "EDI", context.Edi, 4);
    WriteMemoryWords(file, "EBX", context.Ebx, 4);
    WriteMemoryWords(file, "Stack ESP", context.Esp, 64);
#else
    WriteMemoryWords(file, "Stack RSP", static_cast<std::uintptr_t>(context.Rsp), 64);
#endif
}

void WriteSymbol(FILE* file, HANDLE process, DWORD64 address) {
    DWORD64 moduleBase = 0;
    const std::string moduleName = ModuleNameFromAddress(address, &moduleBase);
    const DWORD64 rva = moduleBase ? address - moduleBase : 0;

    std::array<unsigned char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbolBuffer {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    const BOOL hasSymbol = SymFromAddr(process, address, &displacement, symbol);

    IMAGEHLP_LINE64 line {};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0;
    const BOOL hasLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line);

    std::fprintf(file,
                 "%s %s!+%s",
                 Hex(address, sizeof(void*) == 4 ? 8 : 16).c_str(),
                 moduleName.c_str(),
                 Hex(rva).c_str());
    if (hasSymbol) {
        std::fprintf(file, " %s+%llu", symbol->Name, static_cast<unsigned long long>(displacement));
        if ((symbol->Flags & SYMFLAG_EXPORT) != 0) {
            std::fprintf(file, " [nearest export; function identity unverified]");
        }
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

    STACKFRAME64 frame {};
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

bool IsIgnoredFirstChanceException(DWORD code) {
    constexpr DWORD kDbgPrintException = 0x40010006;
    constexpr DWORD kDbgPrintExceptionWide = 0x4001000A;
    constexpr DWORD kMsvcThreadNameException = 0x406D1388;
    return code == EXCEPTION_BREAKPOINT ||
           code == EXCEPTION_SINGLE_STEP ||
           code == kDbgPrintException ||
           code == kDbgPrintExceptionWide ||
           code == kMsvcThreadNameException;
}

void WriteCrashReport(EXCEPTION_POINTERS* info, const char* source) {
    if (!info || !info->ExceptionRecord || !info->ContextRecord) {
        return;
    }
    if (InterlockedExchange(&g_LoggedCrash, 1) != 0) {
        return;
    }

    const std::string path = JoinPath(g_Directory, "HookCrashersCrash.log");
    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "wb");
    if (!file) {
        return;
    }

    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    DWORD64 moduleBase = 0;
    const auto exceptionAddress = reinterpret_cast<DWORD64>(record->ExceptionAddress);
    const std::string moduleName = ModuleNameFromAddress(exceptionAddress, &moduleBase);
    const DWORD64 rva = moduleBase ? exceptionAddress - moduleBase : 0;

    std::fprintf(file, "================ HookCrashers Crash Report ================\n");
    WriteTimestamp(file);
    std::fprintf(file, "HookCrashers version: %s\n", Version::kVersion);
    std::fprintf(file, "Source: %s\n", source ? source : "unknown");
    std::fprintf(file, "ThreadId: %lu\n", GetCurrentThreadId());
    std::fprintf(file, "Exception: %s (%s)\n", ExceptionCodeName(record->ExceptionCode), Hex(record->ExceptionCode, 8).c_str());
    std::fprintf(file,
                 "Address: %s %s!+%s\n",
                 Hex(exceptionAddress, sizeof(void*) == 4 ? 8 : 16).c_str(),
                 moduleName.c_str(),
                 Hex(rva).c_str());
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const char* operation = record->ExceptionInformation[0] == 0 ? "read" :
                                record->ExceptionInformation[0] == 1 ? "write" : "execute";
        std::fprintf(file,
                     "AccessViolation: %s at %s\n",
                     operation,
                     Hex(record->ExceptionInformation[1], sizeof(void*) == 4 ? 8 : 16).c_str());
    }
    std::fprintf(file, "ExceptionFlags: %s Parameters: %lu\n", Hex(record->ExceptionFlags, 8).c_str(), record->NumberParameters);
    for (DWORD i = 0; i < record->NumberParameters; ++i) {
        std::fprintf(file, "  Param[%lu]=%s\n", i, Hex(record->ExceptionInformation[i], sizeof(void*) == 4 ? 8 : 16).c_str());
    }
    WriteRegisters(file, *info->ContextRecord);
    WriteNativeMemoryContext(file, *info->ContextRecord);
    WriteStack(file, *info->ContextRecord);
    std::fprintf(file, "===========================================================\n");
    std::fflush(file);
    fclose(file);

    Logger::Error("Crash captured: %s", path.c_str());
}

LONG WINAPI VectoredExceptionHandler(EXCEPTION_POINTERS* info) {
    if (info && info->ExceptionRecord && !IsIgnoredFirstChanceException(info->ExceptionRecord->ExceptionCode)) {
        WriteCrashReport(info, "VectoredExceptionHandler");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* info) {
    WriteCrashReport(info, "UnhandledExceptionFilter");
    if (g_PreviousUnhandledFilter) {
        return g_PreviousUnhandledFilter(info);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void Install(const std::string& directory) {
    g_Directory = directory;
    InterlockedExchange(&g_LoggedCrash, 0);
    if (!g_VectoredHandler) {
        g_VectoredHandler = AddVectoredExceptionHandler(1, VectoredExceptionHandler);
    }
    g_PreviousUnhandledFilter = SetUnhandledExceptionFilter(UnhandledExceptionHandler);
    Logger::Info("CrashHandler installed");
}

void Uninstall() {
    if (g_VectoredHandler) {
        RemoveVectoredExceptionHandler(g_VectoredHandler);
        g_VectoredHandler = nullptr;
    }
    SetUnhandledExceptionFilter(g_PreviousUnhandledFilter);
    g_PreviousUnhandledFilter = nullptr;
    Logger::Info("CrashHandler uninstalled");
}

} // namespace HookCrashers::Core::CrashHandler
