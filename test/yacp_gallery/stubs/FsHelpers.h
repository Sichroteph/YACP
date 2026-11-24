#pragma once
#include <string_view>
namespace FsHelpers {
inline bool extension(std::string_view path, std::string_view suffix) {
  if (path.size() < suffix.size()) return false;
  path.remove_prefix(path.size()-suffix.size());
  for (size_t i=0;i<path.size();++i) {
    char c=path[i]; if(c>='A' && c<='Z') c+=32;
    if (c!=suffix[i]) return false;
  }
  return true;
}
inline bool hasEpubExtension(std::string_view p) { return extension(p,".epub"); }
inline bool hasTxtExtension(std::string_view p) { return extension(p,".txt"); }
inline bool hasMarkdownExtension(std::string_view p) { return extension(p,".md"); }
inline bool hasXtcExtension(std::string_view p) { return extension(p,".xtc"); }
inline bool hasBmpExtension(std::string_view p) { return extension(p,".bmp"); }
}
