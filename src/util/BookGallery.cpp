#include "BookGallery.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#ifndef SIMULATOR
#include <esp_random.h>
#endif

#include <cstdio>
#include <cstring>

namespace {
constexpr char LAST_IMAGE[] = ".last-sleep-bmp";
constexpr size_t NAME_CAPACITY = 256;
struct Names {
  char current[NAME_CAPACITY] = {};
  char selected[NAME_CAPACITY] = {};
  char previous[NAME_CAPACITY] = {};
};

uint32_t galleryRandom() {
#ifdef SIMULATOR
  static uint32_t value = 0x6d2b79f5u;
  value = value * 1664525u + 1013904223u;
  return value;
#else
  return esp_random();
#endif
}

void readPrevious(const char* folder, char (&out)[NAME_CAPACITY]) {
  char path[BookGallery::FOLDER_CAPACITY + sizeof(LAST_IMAGE) + 1];
  std::snprintf(path, sizeof(path), "%s/%s", folder, LAST_IMAGE);
  if (!Storage.exists(path)) return;
  HalFile file;
  if (!Storage.openFileForRead("GALLERY", path, file)) return;
  const int count = file.read(out, sizeof(out) - 1);
  file.close();
  if (count <= 0) {
    LOG_ERR("GALLERY", "Cannot read previous image marker");
    out[0] = 0;
    return;
  }
  size_t length = count;
  while (length && (out[length - 1] == '\r' || out[length - 1] == '\n' || out[length - 1] == 0)) --length;
  out[length] = 0;
}
}  // namespace

bool BookGallery::folderForBook(const std::string_view bookPath, char (&out)[FOLDER_CAPACITY]) {
  out[0] = 0;
  if (!FsHelpers::hasEpubExtension(bookPath) && !FsHelpers::hasTxtExtension(bookPath) &&
      !FsHelpers::hasMarkdownExtension(bookPath) && !FsHelpers::hasXtcExtension(bookPath)) return false;
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : bookPath) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  const size_t slash = bookPath.find_last_of('/');
  auto stem = slash == std::string_view::npos ? bookPath : bookPath.substr(slash + 1);
  const size_t dot = stem.find_last_of('.');
  if (dot != std::string_view::npos && dot > 0) stem = stem.substr(0, dot);
  char label[49] = {};
  size_t length = 0;
  for (const unsigned char c : stem) {
    if (length == sizeof(label) - 1) break;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
      label[length++] = c;
    } else if (length && label[length - 1] != '-') {
      label[length++] = '-';
    }
  }
  while (length && label[length - 1] == '-') --length;
  label[length] = 0;
  const int count = std::snprintf(out, sizeof(out), "%s/%016llx-%s", ROOT,
                                static_cast<unsigned long long>(hash), length ? label : "book");
  return count > 0 && static_cast<size_t>(count) < sizeof(out);
}

bool BookGallery::selectSleepImage(const std::string_view bookPath, char (&out)[IMAGE_PATH_CAPACITY]) {
  out[0] = 0;
  char folder[FOLDER_CAPACITY];
  if (!folderForBook(bookPath, folder) || !Storage.exists(folder)) return false;
  // One fixed, fallible maintenance allocation replaces the growing filename
  // vector. It is freed before bitmap decoding, regardless of gallery size.
  auto names = makeUniqueNoThrow<Names>();
  if (!names) {
    LOG_ERR("GALLERY", "OOM: filename buffers");
    return false;
  }
  readPrevious(folder, names->previous);
  auto directory = Storage.open(folder);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return false;
  }
  bool previousStillValid = false;
  uint32_t candidates = 0;
  for (auto file = directory.openNextFile(); file; file = directory.openNextFile()) {
    names->current[0] = 0;
    const size_t length = file.getName(names->current, sizeof(names->current));
    bool accepted = !file.isDirectory() && length > 0 && length < NAME_CAPACITY - 1 &&
                    names->current[0] != '.' && FsHelpers::hasBmpExtension(names->current);
    if (accepted) {
      Bitmap bitmap(file);
      accepted = bitmap.parseHeaders() == BmpReaderError::Ok;
    }
    file.close();
    if (!accepted) continue;
    if (!std::strcmp(names->current, names->previous)) {
      previousStillValid = true;
      continue;
    }
    if (candidates == UINT32_MAX) break;
    ++candidates;
    // Reservoir sampling retains one name, with no repeated string allocation.
    if (galleryRandom() % candidates == 0) std::memcpy(names->selected, names->current, NAME_CAPACITY);
  }
  directory.close();
  if (!candidates && !previousStillValid) return false;
  const char* selected = candidates ? names->selected : names->previous;
  const int count = std::snprintf(out, sizeof(out), "%s/%s", folder, selected);
  return count > 0 && static_cast<size_t>(count) < sizeof(out);
}

bool BookGallery::rememberSleepImage(const std::string_view imagePath) {
  const size_t slash = imagePath.find_last_of('/');
  if (slash == std::string_view::npos || slash >= FOLDER_CAPACITY || slash + 1 == imagePath.size() ||
      imagePath.substr(0, sizeof(ROOT) - 1) != ROOT || imagePath.size() <= sizeof(ROOT) ||
      imagePath[sizeof(ROOT) - 1] != '/') return false;
  const auto name = imagePath.substr(slash + 1);
  char path[FOLDER_CAPACITY + sizeof(LAST_IMAGE) + 1];
  std::snprintf(path, sizeof(path), "%.*s/%s", static_cast<int>(slash), imagePath.data(), LAST_IMAGE);
  HalFile file;
  if (!Storage.openFileForWrite("GALLERY", path, file)) return false;
  const bool written = file.write(reinterpret_cast<const uint8_t*>(name.data()), name.size()) == name.size();
  const bool closed = file.close();
  if (!written || !closed) LOG_ERR("GALLERY", "Cannot save previous image marker");
  return written && closed;
}
