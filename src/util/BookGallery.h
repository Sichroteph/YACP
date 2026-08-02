#pragma once

#include <string>
#include <string_view>

namespace BookGallery {

constexpr const char* ROOT = "/.book-galleries";

bool isSupportedBookPath(std::string_view path);
bool isViewableImagePath(std::string_view path);
bool isGalleryPath(std::string_view path);
std::string galleryPathForBook(std::string_view bookPath);
bool ensureGalleryFolder(std::string_view bookPath, std::string& outPath);
bool firstImageForBook(std::string_view bookPath, std::string& outPath);
bool firstSleepImageForBook(std::string_view bookPath, std::string& outPath);
bool randomSleepImageForBook(std::string_view bookPath, std::string& outPath);
bool hasImagesForBook(std::string_view bookPath);

}  // namespace BookGallery
