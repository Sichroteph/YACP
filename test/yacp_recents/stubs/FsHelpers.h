#pragma once
#include <string>
namespace FsHelpers {
inline bool hasEpubExtension(const std::string& p) { return p.ends_with(".epub"); }
inline bool hasXtcExtension(const std::string& p) { return p.ends_with(".xtc"); }
inline bool hasTxtExtension(const std::string& p) { return p.ends_with(".txt"); }
inline bool hasMarkdownExtension(const std::string& p) { return p.ends_with(".md"); }
}
