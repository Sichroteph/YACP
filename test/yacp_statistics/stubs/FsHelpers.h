#pragma once
#include <string>
namespace FsHelpers {
inline bool hasEpubExtension(const std::string& value) { return value.ends_with(".epub"); }
inline bool hasXtcExtension(const std::string& value) { return value.ends_with(".xtc"); }
}
