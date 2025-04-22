#include "SWFArgumentReader.h"
#include "../../Native/NativeFunctions.h"
#include <string>
#include "../../Util/Logger.h"

namespace HookCrashers {
    namespace SWF {
        namespace Helpers {
            static Util::Logger& L = Util::Logger::Instance();

            int32_t SWFArgumentReader::GetInteger(const Data::SWFArgument* arg, int32_t defaultVal) {
                if (!arg || arg->type != Data::SWFArgument::Type::Integer)
                    return defaultVal;
                return arg->value.intValue;
            }

            bool SWFArgumentReader::GetBoolean(const Data::SWFArgument* arg, bool defaultVal) {
                // Booleans might be passed as integers (0 or 1) or specific bool type
                if (!arg) return defaultVal;
                if (arg->type == Data::SWFArgument::Type::Boolean)
                    return arg->value.boolValue != 0;
                // Handle if game passes bools as integers
                if (arg->type == Data::SWFArgument::Type::Integer)
                    return arg->value.intValue != 0;
                return defaultVal;
            }

            float SWFArgumentReader::GetFloat(const Data::SWFArgument* arg, float defaultVal) {
                if (!arg || arg->type != Data::SWFArgument::Type::Float)
                    return defaultVal;
                return arg->value.floatValue;
            }

            uint16_t SWFArgumentReader::GetStringId(const Data::SWFArgument* arg, uint16_t defaultVal) {
                if (!arg || arg->type != Data::SWFArgument::Type::String)
                    return defaultVal;
                return arg->value.stringId;
            }

            std::string SWFArgumentReader::GetString(const Data::SWFArgument* arg, const std::string& defaultVal) {
                if (!arg || arg->type != Data::SWFArgument::Type::String)
                    return defaultVal;
                // Use the native lookup function (currently a placeholder)
                // const char* gameStr = Native::GetGameStringById(arg->value.stringId);
                L.Get()->info("Param 0", arg->value.stringId);
                const char* gameStr = "Yet to be implemented";
                return gameStr ? std::string(gameStr) : defaultVal;
            }


            std::string SWFArgumentReader::GetValueAsString(const Data::SWFArgument* arg) {
                if (!arg) return "[Null Arg]";

                switch (arg->type) {
                case Data::SWFArgument::Type::Boolean:
                    return GetBoolean(arg) ? "true" : "false";
                case Data::SWFArgument::Type::Integer:
                    return std::to_string(GetInteger(arg));
                case Data::SWFArgument::Type::Float:
                    // Format float for better readability
                    char floatBuf[64];
                    snprintf(floatBuf, sizeof(floatBuf), "%.4f", GetFloat(arg));
                    return std::string(floatBuf);
                case Data::SWFArgument::Type::String:
                    // Use the GetString which includes the native lookup attempt
                    return "\"" + GetString(arg, "[Invalid String ID]") + "\"";
                    // Add cases for other types (Object, Null, Undefined) if they exist
                case Data::SWFArgument::Type::Unknown:
                default:
                    return "[Unknown Type:" + std::to_string(static_cast<int>(arg->type)) + " Value:" + std::to_string(arg->value.rawValue) + "]";
                }
            }

        }
    }
}