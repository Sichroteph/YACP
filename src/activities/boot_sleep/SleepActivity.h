#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "images/MoonIcon.h"

#include "activities/Activity.h"

class Bitmap;
class HalFile;
class SleepImageCache;

enum class SleepBitmapRender { None, Monochrome, Grayscale };

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout,
                         uint8_t readerOrientation, bool popupAlreadyShown)
      : Activity("Sleep", renderer, mappedInput),
        fromTimeout(fromTimeout),
        readerOrientation(readerOrientation),
        popupAlreadyShown(popupAlreadyShown) {}
  void onEnter() override;
  SleepBitmapRender bitmapRender() const { return renderedBitmap; }
  static bool usesQuickResume(bool fromTimeout);
  static bool showPreparationPopup(GfxRenderer& renderer);

  // A logical rectangle can straddle one extra byte on every physical row.
  static constexpr size_t MOON_BACKGROUND_BYTES = MOONICON_HEIGHT * ((MOONICON_WIDTH + 14) / 8);
  static bool copyMoonBackground(GfxRenderer& renderer, uint8_t* data, bool restore);

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
  bool popupAlreadyShown;
  mutable SleepBitmapRender renderedBitmap = SleepBitmapRender::None;
  // Cover under a transparent overlay, optionally redrawn for absolute gray planes.
  mutable const Bitmap* overlayCover = nullptr;
  mutable const Bitmap* overlayGrayCover = nullptr;
};
