#pragma once

#include <string_view>

namespace book_cache {
enum class Kind { Unknown, Epub, Xtc, Txt };
enum class Directory { Root, Sections, Html };

inline bool digits(std::string_view text) {
  if (text.empty()) return false;
  for (char c : text) if (c < '0' || c > '9') return false;
  return true;
}
inline Kind kind(std::string_view name) {
  if (name.size() > 25) return Kind::Unknown;
  if (name.substr(0, 5) == "epub_" && digits(name.substr(5))) return Kind::Epub;
  if (name.size() <= 24 && name.substr(0, 4) == "xtc_" && digits(name.substr(4))) return Kind::Xtc;
  if (name.size() <= 24 && name.substr(0, 4) == "txt_" && digits(name.substr(4))) return Kind::Txt;
  return Kind::Unknown;
}
inline bool numbered(std::string_view name, std::string_view prefix, std::string_view suffix) {
  return name.size() > prefix.size() + suffix.size() && name.substr(0, prefix.size()) == prefix &&
         name.substr(name.size() - suffix.size()) == suffix &&
         digits(name.substr(prefix.size(), name.size() - prefix.size() - suffix.size()));
}
inline bool image(std::string_view name) {
  if (name.substr(0, 4) != "img_") return false;
  const auto separator = name.find('_', 4);
  const auto dot = name.find('.', separator == name.npos ? name.size() : separator);
  if (separator == name.npos || dot == name.npos || !digits(name.substr(4, separator - 4)) ||
      !digits(name.substr(separator + 1, dot - separator - 1))) return false;
  const auto extension = name.substr(dot);
  char lower[6]{};
  if (extension.size() >= sizeof(lower)) return false;
  for (size_t i = 0; i < extension.size(); ++i) {
    const char c = extension[i];
    lower[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
  }
  const std::string_view ext(lower);
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".pxc";
}
inline bool generated(Kind type, Directory directory, std::string_view name) {
  if (type == Kind::Unknown) return false;
  if (directory == Directory::Sections)
    return type == Kind::Epub && (numbered(name, "", ".bin") || numbered(name, "", ".bin.part"));
  if (directory == Directory::Html)
    return type == Kind::Epub && (numbered(name, "", ".html") || numbered(name, ".tmp_", ".html"));
  if (name == "cover.bmp" || numbered(name, "thumb_", ".bmp")) return true;
  if (type == Kind::Txt) return name == "index.bin";
  if (type != Kind::Epub) return false;
  return name == "book.bin" || name == "spine.bin.tmp" || name == "toc.bin.tmp" || name == ".items.bin" ||
         name == "css_rules.cache" || name == "css_rules.cache.tmp" || name == "css_rules.cache.bak" ||
         name == "cover_original.bmp" || name == "cover_original_crop.bmp" ||
         name == "cover_legacy_v2.bmp" || name == "cover_legacy_v2_crop.bmp" || image(name);
}
}  // namespace book_cache
