#include "BookGallery.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr uint64_t FNV64_OFFSET = 14695981039346656037ull;
constexpr uint64_t FNV64_PRIME = 1099511628211ull;
constexpr size_t MAX_LABEL_CHARS = 48;
constexpr const char* LAST_SLEEP_MARKER = ".last-sleep-bmp";

uint64_t fnv1a64(std::string_view text) {
  uint64_t hash = FNV64_OFFSET;
  for (const char c : text) {
    hash ^= static_cast<uint8_t>(c);
    hash *= FNV64_PRIME;
  }
  return hash;
}

std::string fileStem(std::string_view path) {
  const size_t slash = path.find_last_of('/');
  std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string_view::npos && dot > 0) {
    name = name.substr(0, dot);
  }
  return std::string(name);
}

std::string safeLabel(std::string_view path) {
  std::string label;
  label.reserve(MAX_LABEL_CHARS);
  for (const char c : fileStem(path)) {
    if (label.size() >= MAX_LABEL_CHARS) break;
    const auto ch = static_cast<unsigned char>(c);
    if (std::isalnum(ch) || c == '-' || c == '_') {
      label.push_back(c);
    } else if (!label.empty() && label.back() != '-') {
      label.push_back('-');
    }
  }
  while (!label.empty() && label.back() == '-') {
    label.pop_back();
  }
  return label.empty() ? "book" : label;
}

bool ensureDirectory(const char* path) {
  if (Storage.exists(path)) {
    HalFile dir = Storage.open(path);
    if (!dir) return false;
    const bool ok = dir.isDirectory();
    dir.close();
    return ok;
  }
  return Storage.mkdir(path);
}

std::string pathInFolder(const std::string& folderPath, const char* filename) {
  std::string path = folderPath;
  if (path.empty() || path.back() != '/') path += "/";
  path += filename;
  return path;
}

std::string readLastSleepName(const std::string& folderPath) {
  FsFile file;
  if (!Storage.openFileForRead("BookGallery", pathInFolder(folderPath, LAST_SLEEP_MARKER), file)) {
    return {};
  }

  char buffer[256] = {};
  const int read = file.read(buffer, sizeof(buffer) - 1);
  file.close();
  if (read <= 0) return {};

  size_t len = static_cast<size_t>(read);
  while (len > 0 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n' || buffer[len - 1] == '\0')) {
    --len;
  }
  return std::string(buffer, len);
}

void writeLastSleepName(const std::string& folderPath, const std::string& filename) {
  FsFile file;
  if (!Storage.openFileForWrite("BookGallery", pathInFolder(folderPath, LAST_SLEEP_MARKER), file)) {
    LOG_ERR("BookGallery", "Failed to write last sleep image marker");
    return;
  }

  if (file.write(filename.data(), filename.size()) != filename.size()) {
    LOG_ERR("BookGallery", "Failed to persist last sleep image marker");
  }
  file.close();
}

size_t chooseRandomIndexAvoidingLast(const std::vector<std::string>& names, const std::string& lastName) {
  if (names.size() <= 1) return 0;

  size_t lastIndex = names.size();
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i] == lastName) {
      lastIndex = i;
      break;
    }
  }

  if (lastIndex >= names.size()) {
    return static_cast<size_t>(esp_random() % names.size());
  }

  const size_t raw = static_cast<size_t>(esp_random() % (names.size() - 1));
  return raw >= lastIndex ? raw + 1 : raw;
}

}  // namespace

namespace BookGallery {

bool isSupportedBookPath(const std::string_view path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

bool isViewableImagePath(const std::string_view path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path);
}

bool isGalleryPath(const std::string_view path) {
  constexpr std::string_view root(ROOT);
  return path == root || (path.size() > root.size() && path.substr(0, root.size()) == root && path[root.size()] == '/');
}

std::string galleryPathForBook(const std::string_view bookPath) {
  char hashText[17] = {};
  std::snprintf(hashText, sizeof(hashText), "%016llx", static_cast<unsigned long long>(fnv1a64(bookPath)));

  std::string path;
  path.reserve(std::string_view(ROOT).size() + 1 + 16 + 1 + MAX_LABEL_CHARS);
  path = ROOT;
  path += "/";
  path += hashText;
  path += "-";
  path += safeLabel(bookPath);
  return path;
}

bool ensureGalleryFolder(const std::string_view bookPath, std::string& outPath) {
  outPath.clear();
  if (!isSupportedBookPath(bookPath)) {
    LOG_ERR("BookGallery", "Unsupported book path: %.*s", static_cast<int>(bookPath.size()), bookPath.data());
    return false;
  }

  if (!ensureDirectory(ROOT)) {
    LOG_ERR("BookGallery", "Failed to create gallery root: %s", ROOT);
    return false;
  }

  outPath = galleryPathForBook(bookPath);
  if (!ensureDirectory(outPath.c_str())) {
    LOG_ERR("BookGallery", "Failed to create gallery folder: %s", outPath.c_str());
    outPath.clear();
    return false;
  }
  return true;
}

bool firstGalleryFileForBook(const std::string_view bookPath, std::string& outPath,
                             bool (*acceptPath)(std::string_view)) {
  outPath.clear();
  if (!isSupportedBookPath(bookPath)) return false;

  const std::string folderPath = galleryPathForBook(bookPath);
  HalFile dir = Storage.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  char name[256];
  for (HalFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    name[0] = '\0';
    file.getName(name, sizeof(name));
    const bool matches = !file.isDirectory() && name[0] != '.' && acceptPath(name);
    file.close();
    if (matches) {
      outPath = folderPath;
      if (outPath.back() != '/') outPath += "/";
      outPath += name;
      dir.close();
      return true;
    }
  }

  dir.close();
  return false;
}

bool firstImageForBook(const std::string_view bookPath, std::string& outPath) {
  return firstGalleryFileForBook(bookPath, outPath, isViewableImagePath);
}

bool firstSleepImageForBook(const std::string_view bookPath, std::string& outPath) {
  return firstGalleryFileForBook(bookPath, outPath, FsHelpers::hasBmpExtension);
}

bool randomSleepImageForBook(const std::string_view bookPath, std::string& outPath) {
  outPath.clear();
  if (!isSupportedBookPath(bookPath)) return false;

  const std::string folderPath = galleryPathForBook(bookPath);
  HalFile dir = Storage.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  std::vector<std::string> names;
  names.reserve(8);
  char name[256];
  for (HalFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    name[0] = '\0';
    file.getName(name, sizeof(name));
    const bool matches = !file.isDirectory() && name[0] != '.' && FsHelpers::hasBmpExtension(name);
    file.close();
    if (matches) {
      names.emplace_back(name);
    }
  }
  dir.close();

  if (names.empty()) {
    return false;
  }

  const size_t index = chooseRandomIndexAvoidingLast(names, readLastSleepName(folderPath));
  outPath = folderPath;
  if (outPath.back() != '/') outPath += "/";
  outPath += names[index];
  writeLastSleepName(folderPath, names[index]);
  return true;
}

bool hasImagesForBook(const std::string_view bookPath) {
  std::string ignored;
  return firstImageForBook(bookPath, ignored);
}

}  // namespace BookGallery
