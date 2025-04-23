#include "SWFArgumentReader.h"
#include "../../Native/NativeFunctions.h"
#include "../../Util/StringCache.h"
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
                if (!arg) return defaultVal;
                if (arg->type == Data::SWFArgument::Type::Boolean)
                    return arg->value.boolValue != 0;
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
                uint16_t stringId = arg->value.stringId;
                return Util::StringCache::LookupString(stringId);
            }


            std::string SWFArgumentReader::GetValueAsString(const Data::SWFArgument* arg) {
                if (!arg) return "[Null Arg]";

                switch (arg->type) {
                case Data::SWFArgument::Type::Boolean:
                    return GetBoolean(arg) ? "true" : "false";
                case Data::SWFArgument::Type::Integer:
                    return std::to_string(GetInteger(arg));
                case Data::SWFArgument::Type::Float:
                    char floatBuf[64];
                    snprintf(floatBuf, sizeof(floatBuf), "%.4f", GetFloat(arg));
                    return std::string(floatBuf);
                case Data::SWFArgument::Type::String:
                    return "\"" + GetString(arg, "[Invalid String ID]") + "\"";
                case Data::SWFArgument::Type::Unknown:
                default:
                    return "[Unknown Type:" + std::to_string(static_cast<int>(arg->type)) + " Value:" + std::to_string(arg->value.rawValue) + "]";
                }
            }

        }
    }
}