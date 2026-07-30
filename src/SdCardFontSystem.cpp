#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  (void)renderer;

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.sdFontResolverCtx = this;

  LOG_DBG("SDFS", "SD font resolver ready; discovery deferred until requested");
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();

  // Lexend/Bitter are YACP's normal path: do not consume a dirty flag, scan a
  // directory, or retain a catalogue when no SD family was explicitly chosen.
  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    loadedFontSizeStep_ = UINT8_MAX;
    loadedTargetPointSize_ = UINT8_MAX;
    return;
  }

  const uint8_t targetPointSize = SETTINGS.getSdFontTargetPointSize();
  const uint8_t sizeStep = SETTINGS.fontSize;
  if (!registryDirty_.load(std::memory_order_acquire) && currentFamily == wantedFamily &&
      loadedFontSizeStep_ == sizeStep && loadedTargetPointSize_ == targetPointSize) {
    return;
  }

  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = ensureRegistry();

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
      loadedFontSizeStep_ = UINT8_MAX;
      loadedTargetPointSize_ = UINT8_MAX;
      return;
    }
    const auto* wantedFile = family->selectFile(targetPointSize, sizeStep);
    uint8_t wantedPt = wantedFile ? wantedFile->pointSize : 0;
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) {
      loadedFontSizeStep_ = sizeStep;
      loadedTargetPointSize_ = targetPointSize;
      return;
    }
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (target %u step %u)%s", wantedFamily, manager_.currentPointSize(),
            wantedPt, targetPointSize, sizeStep, registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, targetPointSize, sizeStep)) {
      loadedFontSizeStep_ = sizeStep;
      loadedTargetPointSize_ = targetPointSize;
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      // A load can fail because the heap is temporarily fragmented. Keep the
      // explicit preference so the resolver can fall back to the built-in font
      // for this session and try the SD family again at the next safe entry.
      LOG_ERR("SDFS", "Failed to load SD font family: %s (selection preserved)", wantedFamily);
      loadedFontSizeStep_ = UINT8_MAX;
      loadedTargetPointSize_ = UINT8_MAX;
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.saveToFile();
    loadedFontSizeStep_ = UINT8_MAX;
    loadedTargetPointSize_ = UINT8_MAX;
  }
}

void SdCardFontSystem::releaseLoadedFont(GfxRenderer& renderer) {
  if (manager_.currentFamilyName().empty()) return;

  const std::string familyName = manager_.currentFamilyName();
  (void)familyName;
  manager_.unloadAll(renderer);
  loadedFontSizeStep_ = UINT8_MAX;
  loadedTargetPointSize_ = UINT8_MAX;
  LOG_DBG("SDFS", "Released SD card font before low-memory operation: %s", familyName.c_str());
}

bool SdCardFontSystem::ensureRegistry() {
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryLoaded_ && !registryWasDirty) {
    return false;
  }

  if (registryWasDirty) {
    LOG_DBG("SDFS", "Registry dirty - re-discovering fonts");
  }
  registry_.discover();
  registryLoaded_ = true;
  return registryWasDirty;
}

void SdCardFontSystem::rediscoverRegistry() {
  registryDirty_.store(true, std::memory_order_release);
  (void)ensureRegistry();
}

void SdCardFontSystem::releaseRegistry() {
  if (!registryLoaded_ && registry_.getFamilyCount() == 0) {
    return;
  }

#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  const int familyCount = registry_.getFamilyCount();
#endif
  registry_.clear();
  registryLoaded_ = false;
  LOG_DBG("SDFS", "Released SD font registry (%d families)", familyCount);
}

void SdCardFontSystem::releaseForNetwork(GfxRenderer& renderer) {
  releaseLoadedFont(renderer);

  releaseRegistry();
  registryDirty_.store(true, std::memory_order_release);
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*fontSizeEnum*/) const {
  // The manager loads exactly one size (closest to SETTINGS.fontSize), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}

bool SdCardFontSystem::changeReaderFontSize(const bool larger) {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    ensureRegistry();
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      const auto sizes = family->availableSizes();
      if (sizes.size() > 1) {
        uint8_t current = SETTINGS.fontSize < sizes.size() ? SETTINGS.fontSize : static_cast<uint8_t>(sizes.size() - 1);
        if (larger) {
          current = static_cast<uint8_t>((current + 1) % sizes.size());
        } else {
          current = current == 0 ? static_cast<uint8_t>(sizes.size() - 1) : static_cast<uint8_t>(current - 1);
        }
        SETTINGS.fontSize = current;
        return true;
      }
    }
  }

  return SETTINGS.changeReaderFontSize(larger);
}
