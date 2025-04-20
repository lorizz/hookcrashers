#include "logger.h"
#include <iostream>

// Initialize static member
std::unique_ptr<Logger, Logger::Deleter> Logger::_instance;

Logger& Logger::Instance()
{
    if (!_instance)
    {
        _instance = std::unique_ptr<Logger, Deleter>(new Logger());
    }
    return *_instance;
}

void Logger::InitializeConsole()
{
    if (!_consoleAllocated)
    {
        AllocConsole();
        freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
        freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        _logger->sinks().push_back(console_sink);

        _consoleAllocated = true;
    }
}

void Logger::Shutdown()
{
    if (_consoleAllocated)
    {
        fclose(stdout);
        fclose(stderr);
        FreeConsole();
    }
}

Logger::Logger()
{
    try
    {
        // Clear log file on startup
        FILE* f;
        if (fopen_s(&f, "../HookCrashers.log", "w") == 0 && f)
        {
            fclose(f);
        }

        // Setup file logging
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("../HookCrashers.log", true);
        _logger = std::make_shared<spdlog::logger>("HookCrashers", file_sink);

        // Default pattern
        _logger->set_pattern("[%T.%f] [%^%l%$] %v");
        _logger->set_level(spdlog::level::trace);
        _logger->flush_on(spdlog::level::info);
    }
    catch (...)
    {
        MessageBoxA(NULL, "Logger initialization failed!", "Error", MB_ICONERROR);
        std::exit(EXIT_FAILURE);
    }
}

Logger::~Logger()
{
    Shutdown();
}

spdlog::logger* Logger::Get()
{
    return _logger.get();
}