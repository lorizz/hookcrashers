#pragma once

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>

// Standard Library
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <cstdio> // For snprintf

// External Libraries
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <detours.h>

// Common Project Headers (Optional - can include directly where needed)
//#include "Util/Logger.h" // Be careful with including loggers in widely used headers