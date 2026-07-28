#pragma once

#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <atomic>
#include <cstdint>

class GfxRenderer;

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  SdCardFontSystem() = default;
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Register the settings resolver. Discovery and loading stay deferred.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Built-in fonts return without touching the SD-card font directories.
  void ensureLoaded(GfxRenderer& renderer);

  /// Temporarily unload the active SD font without clearing the saved setting.
  /// Call ensureLoaded() later to restore it before reader rendering.
  void releaseLoadedFont(GfxRenderer& renderer);

  /// Discover the SD font catalogue on demand. Returns true when a dirty
  /// catalogue forced rediscovery and the active font may need reloading.
  bool ensureRegistry();

  /// Force an immediate catalogue refresh after an explicit install/delete.
  void rediscoverRegistry();

  /// Release catalogue allocations without unloading the active reader font.
  void releaseRegistry();

  /// Release all SD-font RAM that network/TLS work does not need.
  void releaseForNetwork(GfxRenderer& renderer);

  /// Resolve an SD card font ID from family name + fontSize enum.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t fontSizeEnum) const;

  /// Change the reader font size using the active SD family when one is selected.
  bool changeReaderFontSize(bool larger);

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  const SdCardFontRegistry& registry() const { return registry_; }

  /// Non-const access discovers the catalogue first (for UI and FontInstaller).
  SdCardFontRegistry& registry() {
    (void)ensureRegistry();
    return registry_;
  }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() { registryDirty_.store(true, std::memory_order_release); }

  /// Ensure the catalogue reflects any uploaded/deleted fonts.
  void refreshIfDirty() { (void)ensureRegistry(); }

 private:
  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  std::atomic<bool> registryDirty_{false};
  bool registryLoaded_ = false;
  uint8_t loadedFontSizeStep_ = UINT8_MAX;
  uint8_t loadedTargetPointSize_ = UINT8_MAX;
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
