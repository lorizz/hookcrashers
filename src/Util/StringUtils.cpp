#include "StringUtils.h"
#include <sstream>
#include <iomanip>

namespace HookCrashers {
    namespace Util {

        std::string BytesToHex(const unsigned char* data, size_t size) {
            if (!data || size == 0) {
                return "[No Data]";
            }

            std::stringstream ss;
            ss << std::hex << std::setfill('0');

            for (size_t i = 0; i < size; ++i) {
                ss << std::setw(2) << static_cast<int>(data[i]);
                if (i < size - 1) {
                    ss << " ";
                }
            }
            return ss.str();
        }

        std::string BytesToHex(const std::vector<unsigned char>& data) {
            return BytesToHex(data.data(), data.size());
        }

        std::vector<unsigned char> HexToBytes(const std::string& hexString) {
            std::vector<unsigned char> bytes;
            std::string cleanedHexString;

            for (char c : hexString) {
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    cleanedHexString += c;
                }
            }

            if (cleanedHexString.length() % 2 != 0) {
                throw std::invalid_argument("Hex string must have an even number of digits.");
            }

            bytes.reserve(cleanedHexString.length() / 2);

            for (size_t i = 0; i < cleanedHexString.length(); i += 2) {
                std::string byteString = cleanedHexString.substr(i, 2);
                unsigned int byteValue;
                try {
                    byteValue = std::stoul(byteString, nullptr, 16);
                }
                catch (const std::invalid_argument& e) {
                    throw std::invalid_argument("Invalid non-hex character found in string: " + byteString);
                }
                catch (const std::out_of_range& e) {
                    throw std::invalid_argument("Hex value out of range: " + byteString);
                }

                if (byteValue > 255) {
                    throw std::invalid_argument("Hex value out of range for unsigned char: " + byteString);
                }
                bytes.push_back(static_cast<unsigned char>(byteValue));
            }

            return bytes;
        }

        std::vector<unsigned char> StringToBytes(const std::string& str) {
            return std::vector<unsigned char>(str.begin(), str.end());
        }

        std::string BytesToString(const unsigned char* data, size_t size) {
            if (!data || size == 0) {
                return "";
            }

            size_t actual_length = 0;
            while (actual_length < size && data[actual_length] != '\0') {
                actual_length++;
            }

            if (actual_length == 0) {
                return "";
            }

            try {
                return std::string(reinterpret_cast<const char*>(data), actual_length);
            }
            catch (const std::exception& e) {
                return "[Conversion Error]";
            }
        }

        std::string BytesToString(const std::vector<unsigned char>& data) {
            return BytesToString(data.data(), data.size());
        }
    }
}