#pragma once
#include <string>
// Serialization test only. Credential envelope conversion is tested separately.
namespace obfuscation {
inline std::string obfuscateToBase64(const std::string& value) { return value; }
inline std::string deobfuscateFromBase64(const std::string& value) { return value; }
}
