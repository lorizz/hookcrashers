#pragma once

#include "../stdafx.h"
#include <memory>
#include <mutex>

namespace spdlog { class logger; }

namespace HookCrashers {
	namespace Util {
		struct LogEntry {
			std::string level;
			std::string scope;
			std::string message;
		};

		class Logger {
		public:
			static Logger& Instance();
			static void SetDefaultLogDirectory(const std::string& directory);

			spdlog::logger* Get();
			void InitializeConsole();
			void Shutdown();
			std::vector<LogEntry> GetBufferedEntries() const;
			void AddBufferedEntry(const std::string& level, const std::string& message);
			void SetScope(const std::string& scope);
			void ClearScope();
			std::string GetCurrentScope() const;

			class ScopedLogContext {
			public:
				explicit ScopedLogContext(const std::string& scope);
				~ScopedLogContext();

				ScopedLogContext(const ScopedLogContext&) = delete;
				ScopedLogContext& operator=(const ScopedLogContext&) = delete;

			private:
				std::string _previousScope;
			};

		private:
			Logger();
			~Logger();

			Logger(const Logger&) = delete;
			Logger& operator=(const Logger&) = delete;

			struct Deleter {
				void operator()(Logger* p) const { delete p; }
			};
			static std::unique_ptr<Logger, Deleter> _instance;

			void PositionConsoleOnSecondaryMonitor();

			std::shared_ptr<spdlog::logger> _logger;
			static std::string _logDirectory;
			bool _consoleAllocated = false;
			static uintptr_t _moduleBase;
			mutable std::mutex _bufferMutex;
			std::vector<LogEntry> _entries;
		};
	}
}
