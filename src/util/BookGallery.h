#pragma once

#include <cstddef>
#include <string_view>

namespace BookGallery {
constexpr char ROOT[] = "/.book-galleries";
constexpr size_t FOLDER_CAPACITY = 96;
constexpr size_t IMAGE_PATH_CAPACITY = 384;

// Preserve the original YACP FNV-1a identity, independent of std::hash/MCU.
bool folderForBook(std::string_view bookPath, char (&out)[FOLDER_CAPACITY]);
// Bounded scan; excludes the previous valid BMP whenever another one exists.
bool selectSleepImage(std::string_view bookPath, char (&out)[IMAGE_PATH_CAPACITY]);
bool rememberSleepImage(std::string_view imagePath);
}  // namespace BookGallery
