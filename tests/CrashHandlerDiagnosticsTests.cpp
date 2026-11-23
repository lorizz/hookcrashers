// Exercise the real crash reporter with a controlled execute-at-1 exception.
// No game process or proprietary fixtures are used.
#include "../src/Core/CrashHandler.cpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace HookCrashers::Core::Logger {
void Info(const char*, ...) {}
void Error(const char*, ...) {}
}

volatile std::uintptr_t invalidTarget = 1;
__declspec(noinline) void ExecuteInvalidTarget() {
    __try {
        reinterpret_cast<void(*)()>(invalidTarget)();
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}

int main(int argc, char** argv) {
    using namespace HookCrashers::Core::CrashHandler;
    Check(argc == 2, "output directory argument");
    Install(argv[1]);
    ExecuteInvalidTarget();
    Uninstall();
    std::ifstream input(JoinPath(argv[1], "HookCrashersCrash.log"));
    const std::string report((std::istreambuf_iterator<char>(input)), {});
    Check(report.find("AccessViolation: execute at 0x00000001") != std::string::npos, "execute fault reported");
    Check(report.find("HookCrashers version:") != std::string::npos, "version recorded");
    Check(report.find("Native memory context") != std::string::npos &&
          report.find("Stack ESP") != std::string::npos && report.find("Callstack:") != std::string::npos,
          "register memory, raw stack and unwind survive execute fault");

    SYSTEM_INFO info {}; GetSystemInfo(&info);
    auto* region = static_cast<unsigned char*>(VirtualAlloc(nullptr, info.dwPageSize * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(region != nullptr, "memory fixture allocated");
    DWORD old = 0;
    Check(VirtualProtect(region + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &old) != 0,
          "second page inaccessible");
    auto* edge = region + info.dwPageSize - 4;
    *reinterpret_cast<std::uint32_t*>(edge) = 0x12345678;
    FILE* sample = std::tmpfile(); Check(sample != nullptr, "sample output");
    WriteMemoryWords(sample, "edge", reinterpret_cast<std::uintptr_t>(edge), 8);
    WriteMemoryWords(sample, "bad", 1, 8);
    WriteMemoryWords(sample, "overflow", UINTPTR_MAX - 1, 8);
    std::rewind(sample);
    char buffer[2048] {};
    std::fread(buffer, 1, sizeof(buffer) - 1, sample); std::fclose(sample);
    Check(std::strstr(buffer, "+00 12345678") && std::strstr(buffer, "+04 <unreadable>"),
          "bounded sample stops safely at inaccessible page");
    VirtualFree(region, 0, MEM_RELEASE);
    std::puts("PASS actual execute-at-1 VEH capture, version, register/stack memory, unwind and unreadable/overflow addresses");
}
