#include "Logger.h"
#include <iostream>
#include <cstdio>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/null_sink.h>

// Struct & Callback to find the secondary monitor
struct MonitorInfo {
	bool foundSecondary = false;
	RECT secondaryMonitorRect{};
};

BOOL CALLBACK MonitorEnumProc(HMONITOR h, HDC, LPRECT, LPARAM dwData) {
	auto* mi = reinterpret_cast<MonitorInfo*>(dwData);
	MONITORINFOEXA monInfo;
	monInfo.cbSize = sizeof(MONITORINFOEXA);
	GetMonitorInfoA(h, &monInfo);
	if (!(monInfo.dwFlags & MONITORINFOF_PRIMARY)) {
		mi->foundSecondary = true;
		mi->secondaryMonitorRect = monInfo.rcWork;
		return FALSE;
	}
	return TRUE;
}

namespace HookCrashers {
	namespace Util {
		namespace {
			thread_local std::string g_logScope;
			class RingBufferSink final : public spdlog::sinks::base_sink<std::mutex> {
			public:
				explicit RingBufferSink(Logger* owner) : _owner(owner) {}

			protected:
				void sink_it_(const spdlog::details::log_msg& msg) override {
					spdlog::memory_buf_t formatted;
					formatter_->format(msg, formatted);
					if (_owner) {
						_owner->AddBufferedEntry(
							std::string(spdlog::level::to_string_view(msg.level).data(), spdlog::level::to_string_view(msg.level).size()),
							std::string(formatted.data(), formatted.size()));
					}
				}

				void flush_() override {}

			private:
				Logger* _owner = nullptr;
			};
		}

		std::unique_ptr<Logger, Logger::Deleter> Logger::_instance;
		uintptr_t Logger::_moduleBase = 0;
		std::string Logger::_logDirectory;

		Logger& Logger::Instance() {
			if (!_instance) {
				_instance = std::unique_ptr<Logger, Deleter>(new Logger());
			}
			return *_instance;
		}

		void Logger::InitializeConsole() {
			if (!_consoleAllocated) {
				if (AllocConsole() || AttachConsole(ATTACH_PARENT_PROCESS)) {
					FILE* fp;
					freopen_s(&fp, "CONOUT$", "w", stdout);
					freopen_s(&fp, "CONOUT$", "w", stderr);
					freopen_s(&fp, "CONIN$", "r", stdin);
					std::cout.clear();
					std::cerr.clear();
					std::cin.clear();

					// Posiziona la console sul secondo schermo se disponibile
					PositionConsoleOnSecondaryMonitor();

					auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
					_logger->sinks().push_back(console_sink);
					_consoleAllocated = true;
					Get()->info("Console logger initialized.");
				}
				else {
					Get()->error("Failed to allocate or attach console. GetLastError={}", GetLastError());
				}
			}
		}

		void Logger::PositionConsoleOnSecondaryMonitor() {
			SetConsoleTitleA("HookCrashers Log");

			MonitorInfo mi;
			EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&mi));

			if (mi.foundSecondary) {
				HWND consoleWindow = GetConsoleWindow();
				if (consoleWindow) {
					// Posiziona la console nell'angolo in alto a sinistra del secondo monitor
					SetWindowPos(consoleWindow, NULL,
						mi.secondaryMonitorRect.left,
						mi.secondaryMonitorRect.top,
						0, 0,
						SWP_NOSIZE | SWP_NOZORDER);

					Get()->info("Console positioned on secondary monitor at ({}, {})",
						mi.secondaryMonitorRect.left,
						mi.secondaryMonitorRect.top);
				}
			}
			else {
				Get()->info("Secondary monitor not found, console remains on primary monitor");
			}
		}

		void Logger::Shutdown() {
			Get()->info("Logger shutting down.");
			Get()->flush();
			spdlog::shutdown();
			if (_consoleAllocated) {
				FreeConsole();
				_consoleAllocated = false;
			}
		}

		Logger::Logger() : _consoleAllocated(false) {
			try {
				auto ui_sink = std::make_shared<RingBufferSink>(this);
				std::vector<spdlog::sink_ptr> sinks;
				sinks.push_back(ui_sink);

				try {
					std::string logPath = _logDirectory.empty() ? "HookCrashers.log" : (_logDirectory + "HookCrashers.log");
					auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath, true);
					sinks.push_back(file_sink);
					AddBufferedEntry("info", "[Logger] File sink path: " + logPath);
				}
				catch (const spdlog::spdlog_ex& ex) {
					AddBufferedEntry("warning", std::string("[Logger] File sink disabled: ") + ex.what());
					sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
				}

				_logger = std::make_shared<spdlog::logger>("HookCrashers", sinks.begin(), sinks.end());
				_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
				_logger->set_level(spdlog::level::trace);
				_logger->flush_on(spdlog::level::trace);
				_logger->info("Logger initialized.");
				_logger->flush();
			}
			catch (const spdlog::spdlog_ex& ex) {
				_logger = std::make_shared<spdlog::logger>("HookCrashers", std::make_shared<RingBufferSink>(this));
				_logger->set_level(spdlog::level::trace);
				AddBufferedEntry("error", std::string("[Logger] Logger initialized without file sink: ") + ex.what());
			}
			catch (...) {
				_logger = std::make_shared<spdlog::logger>("HookCrashers", std::make_shared<RingBufferSink>(this));
				_logger->set_level(spdlog::level::trace);
				AddBufferedEntry("error", "[Logger] Logger initialized without file sink due to an unknown error.");
			}
		}

		Logger::~Logger() {}

		spdlog::logger* Logger::Get() {
			return _logger.get();
		}

		void Logger::SetDefaultLogDirectory(const std::string& directory) {
			_logDirectory = directory;
		}

		std::vector<LogEntry> Logger::GetBufferedEntries() const {
			std::lock_guard<std::mutex> lock(_bufferMutex);
			return _entries;
		}

		void Logger::AddBufferedEntry(const std::string& level, const std::string& message) {
			std::lock_guard<std::mutex> lock(_bufferMutex);
			_entries.push_back({ level, g_logScope, message });
			constexpr size_t MaxEntries = 1000;
			if (_entries.size() > MaxEntries) {
				_entries.erase(_entries.begin(), _entries.begin() + (_entries.size() - MaxEntries));
			}
		}

		void Logger::SetScope(const std::string& scope) {
			g_logScope = scope;
		}

		void Logger::ClearScope() {
			g_logScope.clear();
		}

		std::string Logger::GetCurrentScope() const {
			return g_logScope;
		}

		Logger::ScopedLogContext::ScopedLogContext(const std::string& scope) {
			_previousScope = Logger::Instance().GetCurrentScope();
			Logger::Instance().SetScope(scope);
		}

		Logger::ScopedLogContext::~ScopedLogContext() {
			if (_previousScope.empty()) {
				Logger::Instance().ClearScope();
			}
			else {
				Logger::Instance().SetScope(_previousScope);
			}
		}
	}
}
