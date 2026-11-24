#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;
class SleepImageCache;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout,
                         uint8_t readerOrientation)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout), readerOrientation(readerOrientation) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  bool renderBookGallerySleepScreen() const;
  void renderCoverSleepScreen() const;
  bool renderBitmapSleepPath(const char* path) const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false,
                               SleepImageCache* cache = nullptr, HalFile* source = nullptr,
                               const char* sourcePath = nullptr) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
  uint8_t readerOrientation = 0;  // Captured before the EPUB restores global preferences.
  // Cover under a transparent overlay, optionally redrawn for absolute gray planes.
  mutable const Bitmap* overlayCover = nullptr;
  mutable const Bitmap* overlayGrayCover = nullptr;
};
