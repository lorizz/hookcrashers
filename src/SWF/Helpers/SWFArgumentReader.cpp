#include "SWFArgumentReader.h"
#include "../../Util/StringCache.h"
#include "../../Util/Logger.h"
#include <string>

// This alias is now defined in the header, so it's available here
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;

namespace HookCrashers {
    namespace SWF {
        namespace Helpers {
            static Util::Logger& L = Util::Logger::Instance();

            // FIXED: All function signatures and logic updated to use the public type
            int32_t SWFArgumentReader::GetInteger(const HC_SWFArgument* arg, int32_t defaultVal) {
                int32_t convertedArg = SWF::Helpers::SWFArgumentReader::GetFloat(arg, -1);
                return convertedArg;
            }

            bool SWFArgumentReader::GetBoolean(const HC_SWFArgument* arg, bool defaultVal) {
                if (!arg) return defaultVal;
                if (arg->type == HC_SWFArgument::Type::Boolean)
                    return arg->value.boolValue != 0;
                if (arg->type == HC_SWFArgument::Type::Integer)
                    return arg->value.intValue != 0;
                return defaultVal;
            }

            float SWFArgumentReader::GetFloat(const HC_SWFArgument* arg, float defaultVal) {
                if (!arg || arg->type != HC_SWFArgument::Type::Float)
                    return defaultVal;
                return arg->value.floatValue;
            }

            uint16_t SWFArgumentReader::GetStringId(const HC_SWFArgument* arg, uint16_t defaultVal) {
                if (!arg || arg->type != HC_SWFArgument::Type::String)
                    return defaultVal;
                return arg->value.stringId;
            }

            std::string SWFArgumentReader::GetString(const HC_SWFArgument* arg, const std::string& defaultVal) {
                if (!arg || arg->type != HC_SWFArgument::Type::String)
                    return defaultVal;
                uint16_t stringId = arg->value.stringId;
                return Util::StringCache::LookupString(stringId);
            }

            std::string SWFArgumentReader::GetValueAsString(const HC_SWFArgument* arg) {
                if (!arg) return "[Null Arg]";

                switch (arg->type) {
                case HC_SWFArgument::Type::Boolean:
                    return GetBoolean(arg) ? "true" : "false";
                case HC_SWFArgument::Type::Integer:
                    return std::to_string(GetInteger(arg));
                case HC_SWFArgument::Type::Float:
                    char floatBuf[64];
                    snprintf(floatBuf, sizeof(floatBuf), "%.4f", GetFloat(arg));
                    return std::string(floatBuf);
                case HC_SWFArgument::Type::String:
                    return "\"" + GetString(arg, "[Invalid String ID]") + "\"";
                case HC_SWFArgument::Type::Unknown:
                default:
                    return "[Unknown Type:" + std::to_string(static_cast<int>(arg->type)) + " Value:" + std::to_string(arg->value.rawValue) + "]";
                }
            }
        }
    }
}