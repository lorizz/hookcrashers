#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <Windows.h>

class Logger
{
public:
    // Public static accessor
    static Logger& Instance();

    // Public interface
    spdlog::logger* Get();
    void InitializeConsole();
    void Shutdown();

private:
    // Private constructor/destructor
    Logger();
    ~Logger();

    // Make non-copyable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Static instance with custom deleter
    struct Deleter {
        void operator()(Logger* p) const { delete p; }
    };
    static std::unique_ptr<Logger, Deleter> _instance;

    // Member variables
    std::shared_ptr<spdlog::logger> _logger;
    bool _consoleAllocated = false;
};