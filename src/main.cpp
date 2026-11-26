#include <Arduino.h>
#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SPI.h>
#include <TrustedTime.h>
#include <VectorFontSupport.h>
#include <WiFi.h>
#include <XteinkDetect.h>
#include <builtinFonts/all.h>
#include <esp_heap_caps.h>
#ifdef SIMULATOR
#include <CredentialIntegrity.h>
#else
#include <esp_rom_crc.h>
#endif
#include <esp_wifi.h>
#include <time.h>
#include "WifiCredentialStore.h"

#include <atomic>
#include <cstdarg>
#include <cstring>

#include "util/BookPathMove.h"
#include "util/BootTiming.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/reader/ReaderUtils.h"
#include "util/LegacyMigration.h"
#if defined(YACP_PERSONAL_GO)
#include <ActivityLog.h>
#include <GoPersonal.h>
#include <GoService.h>
#include <esp_sleep.h>
#include <esp_system.h>
#endif

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "YacpFonts.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "activities/reader/StatsBackup.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "power/PowerHistory.h"
#include "util/BootShortcut.h"
#include "util/ButtonNavigator.h"
#include "util/PluginEvents.h"
#include "util/ScreenshotUtil.h"
#include "util/SlideSwitch.h"
#include "util/Timezones.h"
#include "util/X3WakePage.h"
#include "util/WakeRefreshTrace.h"

#if CROSSPOINT_VECTOR_FONTS
// Rendering (incl. FreeType TTF rasterization) runs on the Arduino loop task.
// The default 8 KB stack overflows inside FreeType's FT_Open_Face / variable-font
// parsing. This runtime override applies even with the prebuilt (dio_opi) core,
// where CONFIG_ARDUINO_LOOP_STACK_SIZE from sdkconfig is baked in and ignored.
// Vector-font boards only: without TTF the stock loop stack has always sufficed,
// and non-PSRAM boards need the 16KB back in DRAM.
SET_LOOP_TASK_STACK_SIZE(24 * 1024)
#endif

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
static unsigned long allowSleepAt = 0;
static unsigned long lastX4ProPowerClickAt = 0;

namespace {
constexpr unsigned long X4PRO_POWER_DOUBLE_CLICK_MS = 500;
constexpr unsigned long X4PRO_POWER_CLICK_MAX_HOLD_MS = 300;
}  // namespace

// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
#ifndef OMIT_FONTS
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
// This UI-only family is used by UiSliderDialog. FreeInkUI requests regular
// or bold, never italic. Reader families register their own four variants.
EpdFontFamily notosans18FontFamily(18, &notosans18RegularFont, &notosans18BoldFont);

#endif  // OMIT_FONTS

EpdFont smallFont(&ubuntu_8_regular);
EpdFontFamily smallFontFamily(8, &smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(10, &ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(12, &ui12RegularFont, &ui12BoldFont);

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
RTC_NOINIT_ATTR uint32_t silentRebootPayload;

static uint8_t wakeMoonBackground[SleepActivity::MOON_BACKGROUND_BYTES];
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;
constexpr uint32_t SILENT_REBOOT_TARGET_SETTINGS = 2;
constexpr uint32_t SILENT_REBOOT_TARGET_JOIN_NETWORK = 3;
constexpr uint32_t SILENT_REBOOT_TARGET_MAX = SILENT_REBOOT_TARGET_JOIN_NETWORK;
constexpr uint32_t SILENT_REBOOT_LIGHT_ON = 1U << 0;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

// A silent restart is internal maintenance, so the light must come back exactly
// as the user left it. SETTINGS.frontlightOn is the saved preference and
// legitimately diverges from the live state (a wake with Restore Light on Wake
// off leaves the light off while the saved "was on" preference is kept), so
// carry the live state across the reboot instead of re-deriving it from
// settings. Cleared with the magic in setup().
static void armSilentReboot(const uint32_t target) {
  silentRebootTarget = target;
  silentRebootPayload = Frontlight.isOn() ? SILENT_REBOOT_LIGHT_ON : 0;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
}

// Returns instead of rebooting when sleep supersedes the reboot; callers keep
// running in that case.
static void silentRestartTo(const uint32_t target, const char* targetName) {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  armSilentReboot(target);
  LOG_DBG("MAIN", "Silent restart (target=%s)", targetName);
  // E-ink retains the previous frame until the target's first paint lands
  // (~2-3s). Without an overlay, users don't see the reboot and fire input
  // through to the new activity. On Home, Select on the default
  // selectorIndex=0 opens the most-recent book, looking like a trampoline back
  // to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestart() { silentRestartTo(SILENT_REBOOT_TARGET_HOME, "home"); }

void silentRestartToReader() { silentRestartTo(SILENT_REBOOT_TARGET_READER, "reader"); }

void silentRestartToSettings() { silentRestartTo(SILENT_REBOOT_TARGET_SETTINGS, "settings"); }

void silentRestartToJoinNetwork() {
  if (deepSleepInProgress) return;
#if FREEINK_CAP_TOUCH
  // A software reset would cycle touch/frontlight rails; those boards proceed
  // into Join Network without the fresh-heap reboot (return, don't stop WiFi —
  // this runs on the way *in*, unlike the exit-time silentRestart()).
  if (BoardConfig::hasTouch()) return;
#endif
  armSilentReboot(SILENT_REBOOT_TARGET_JOIN_NETWORK);
  LOG_DBG("MAIN", "Silent restart (target=join-network)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;  // sleeping supersedes the storage handoff reboot
  armSilentReboot(SILENT_REBOOT_TARGET_HOME);
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

void toggleFrontlight() {
  if (!Frontlight.present()) return;
  const bool lightOn = !Frontlight.isOn();
  Frontlight.setOn(lightOn);
  SETTINGS.frontlightOn = lightOn ? 1 : 0;
  SETTINGS.saveToFile();
  activityManager.notifyFrontlightChanged();
  LOG_INF("LIGHT", "Frontlight toggled %s", lightOn ? "on" : "off");
}

// Applies the slide switch's position to the bound function at boot, on each
// change, and when its binding changes; the physical position stays the source
// of truth, as on stock.
void handleSlideSwitch() {
  static int appliedAction = -1;
  static int appliedOn = -1;
  if (!gpio.hasToggleSwitch()) return;
  // Compare the position, not the change edge: transfer loops poll input and
  // clear the edge before this runs.
  const bool on = gpio.isToggleSwitchOn();
  if (appliedAction == SETTINGS.slideSwitchAction && appliedOn == static_cast<int>(on)) return;
  appliedAction = SETTINGS.slideSwitchAction;
  appliedOn = on;
  switch (SETTINGS.slideSwitchAction) {
    case CrossPointSettings::SLIDE_SWITCH_NIGHT_MODE:
      if ((SETTINGS.screenInverted != 0) == on) return;
      SETTINGS.screenInverted = on ? 1 : 0;
      break;
    case CrossPointSettings::SLIDE_SWITCH_TOUCH_CONTROLS:
      if ((SETTINGS.touchReaderControls != CrossPointSettings::TOUCH_READER_OFF) == on) return;
      SETTINGS.touchReaderControls = on ? CrossPointSettings::TOUCH_READER_ON : CrossPointSettings::TOUCH_READER_OFF;
      break;
    default:
      // Also compare the saved state: setup() lit the light from the switch
      // without updating it.
      if (!Frontlight.present() || (Frontlight.isOn() == on && (SETTINGS.frontlightOn != 0) == on)) return;
      Frontlight.setOn(on);
      SETTINGS.frontlightOn = on ? 1 : 0;
      break;
  }
  SETTINGS.saveToFile();
  activityManager.requestUpdate();
  LOG_INF("SWITCH", "Slide switch %s", on ? "on" : "off");
}

bool handleX4ProFrontlightDoubleClick() {
  if (!BoardConfig::isX4Pro() || !SETTINGS.doubleClickPwrLight || !gpio.wasReleased(HalGPIO::BTN_POWER)) {
    return false;
  }

  const unsigned long now = millis();
  if (gpio.getPowerButtonHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS) {
    lastX4ProPowerClickAt = 0;
    return false;
  }

  if (lastX4ProPowerClickAt == 0 || now - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = now;
    return false;
  }

  lastX4ProPowerClickAt = 0;
  toggleFrontlight();
  return true;
}

// Bluetooth page turner (lib/BlePageTurner): what the module may ask of this firmware. The module
// owns the radio; loop() tells it what is on screen, and the screen changes and sleep ask it to stop.
namespace pageturner {
std::atomic<bool> holdsCpu{false};

bleturner::Heap heap() {
  const auto h = HalMemory::getInternalHeap();
  return {h.freeBytes, h.largestBlockBytes};
}

// Runs on the radio's start task. Only TRIES the render lock: the render task can hold it through a
// chapter build that is waiting for this very radio.
bool releaseCaches() {
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock()) return false;
  fontCacheManager.releaseSdFontCaches();
  return true;
}

bool deliver(const bleturner::Action action) {
  switch (action) {
    case bleturner::Action::NextPage:
      return activityManager.remoteTurn(true, false);
    case bleturner::Action::PrevPage:
      return activityManager.remoteTurn(false, false);
    case bleturner::Action::NextChapter:
      return activityManager.remoteTurn(true, true);
    case bleturner::Action::PrevChapter:
      return activityManager.remoteTurn(false, true);
    case bleturner::Action::Confirm:
      return activityManager.remoteClick(RemoteButton::Confirm);
    case bleturner::Action::Back:
      return activityManager.remoteClick(RemoteButton::Back);
    case bleturner::Action::ReaderMenu:
      return activityManager.remoteShortcut(true);
    case bleturner::Action::SaveQuote:
      return activityManager.remoteShortcut(false);
    case bleturner::Action::None:
      return false;
  }
  return false;
}

bool yieldForRadio() { return true; }        // the reader keeps no parser alive between pages
bool fileTransferActive() { return false; }  // file transfer is a screen of its own: the radio is off there
void restartIntoBook() { silentRestartToReader(); }

// The radio needs a steady clock while it is up or changing state.
void holdFullSpeed(const bool hold) {
  holdsCpu.store(hold);
  if (hold) powerManager.setPowerSaving(false);
}

void log(const bool error, const char* format, va_list args) {
#ifdef ENABLE_SERIAL_LOG
  if (!error && LOG_LEVEL < 1) return;
  char line[160];
  vsnprintf(line, sizeof(line), format, args);
  logPrintf(error ? "ERR" : "INF", "BLE", "%s", line);
#else
  (void)error;
  (void)format;
  (void)args;
#endif
}

const bleturner::Host host{heap,          releaseCaches, deliver, yieldForRadio, fileTransferActive, restartIntoBook,
                           holdFullSpeed, log,           nullptr};
}  // namespace pageturner

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";
constexpr char COVER_WAKE_FRAME_FILE[] = "/.crosspoint/cover_wake_frame.bin";
constexpr char CUSTOM_WAKE_BASE_FILE[] = "/.crosspoint/custom_wake_base.bin";

static bool readerWakePreviewDisplayed = false;
static std::atomic<bool> captureWakeTurn{false};
static std::atomic<uint8_t> wakeTurnButton{0};

static void captureFirstWakeTurn(void*) {
  constexpr uint8_t sides = (1u << HalGPIO::BTN_UP) | (1u << HalGPIO::BTN_DOWN);
  uint8_t previous = 0;
  uint8_t pressed = 0;
  uint32_t pressedAt = 0;
  while (captureWakeTurn.load(std::memory_order_acquire)) {
    const uint8_t sample = gpio.samplePhysicalSideButtons();
    if (sample == previous) {
      if (sample == 0 && pressed != 0) {
        if (millis() - pressedAt < ReaderUtils::SKIP_HOLD_MS) {
          wakeTurnButton.store(pressed, std::memory_order_release);
          break;
        }
        pressed = 0;
      }
      if (sample != 0 && sample != sides && pressed != sample) {
        pressed = sample;
        pressedAt = millis();
      }
    }
    previous = sample;
    delay(10);
  }
  vTaskDelete(nullptr);
}

static void startWakeTurnCapture() {
  captureWakeTurn.store(true, std::memory_order_release);
  if (xTaskCreate(captureFirstWakeTurn, "Wake first turn", 2048, nullptr, 2, nullptr) != pdPASS) {
    captureWakeTurn.store(false, std::memory_order_release);
    LOG_ERR("BOOT", "Could not buffer the first side-button tap");
  }
}

#if defined(YACP_X3_WAKE_ONLY_TEST)
constexpr char WAKE_ONLY_FRAME[] = "/.crosspoint/x3-wake-only.bin";
constexpr char WAKE_ONLY_PENDING[] = "/.crosspoint/x3-wake-only.tmp";
constexpr char WAKE_ONLY_TIMING[] = "/.crosspoint/x3-wake-only-timing.txt";
static bool wakeOnlyDisplayed = false;

// A separate one-shot experiment. The normal boot path is used to get to the
// sample page. The measured wake ends as soon as the cached image is visible.
static bool showWakeOnlyPage(uint32_t gpioReady, uint32_t sdReady) {
  HalFile file;
  if (!Storage.exists(WAKE_ONLY_FRAME) || !Storage.openFileForRead("WAKETEST", WAKE_ONLY_FRAME, file)) return false;
  const auto panelStart = millis();
  display.begin(true);
  const auto panelReady = millis();
  const size_t size = display.getBufferSize();
  const bool read = display.getFrameBuffer() && size != 0 && file.fileSize() == size &&
                    file.read(display.getFrameBuffer(), size) == static_cast<int>(size);
  const bool closed = file.close();
  if (!read || !closed) {
    Storage.remove(WAKE_ONLY_FRAME);
    LOG_ERR("WAKETEST", "Stored test frame is unreadable");
    return false;
  }
  const auto frameReady = millis();
  display.setInverted(false);
  display.restorePageFromSleepCover(false);
  const auto visible = millis();
  wakeOnlyDisplayed = true;
#if YACP_X3_WAKE_ONLY_TEST == 2
  boot_timing::mark(boot_timing::SavedPageVisible);
  readerWakePreviewDisplayed = true;
  startWakeTurnCapture();
#endif

  // Cleanup and instrumentation stay off the measured path.
  if (!Storage.remove(WAKE_ONLY_FRAME)) LOG_ERR("WAKETEST", "Could not consume test frame");
  const auto elapsed = [](uint32_t end) { return static_cast<unsigned long>(end - boot_timing::started); };
  char timing[160];
  const int length = snprintf(timing, sizeof(timing),
                              "ms from setup: gpio=%lu sd=%lu panel_start=%lu panel_ready=%lu frame=%lu visible=%lu "
                              "controller=%u\n",
                              elapsed(gpioReady), elapsed(sdReady), elapsed(panelStart), elapsed(panelReady),
                              elapsed(frameReady), elapsed(visible), static_cast<unsigned>(display.getController()));
  LOG_INF("WAKETEST", "%s", timing);
  if (length > 0 && static_cast<size_t>(length) < sizeof(timing) &&
      Storage.openFileForWrite("WAKETEST", WAKE_ONLY_TIMING, file)) {
    const bool written = file.write(timing, static_cast<size_t>(length)) == static_cast<size_t>(length);
    if (!file.close() || !written) LOG_ERR("WAKETEST", "Could not save timing");
  }
  return true;
}
#endif

static uint32_t wakeFrameCrc() {
#ifdef SIMULATOR
  return credential_integrity::crc32({reinterpret_cast<const char*>(renderer.getFrameBuffer()),
                                      renderer.getBufferSize()});
#else
  return esp_rom_crc32_le(0, renderer.getFrameBuffer(), renderer.getBufferSize());
#endif
}

static bool saveSleepFrameBuffer(const char* path) {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", path, file)) return false;
  const auto size = renderer.getBufferSize();
  const bool complete = file.write(renderer.getFrameBuffer(), size) == size;
  return file.close() && complete;
}

static bool loadSleepFrameBuffer(const char* path) {
  HalFile file;
  if (!display.getFrameBuffer() || !Storage.openFileForRead("SLP", path, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const bool complete = file.fileSize() == bufferSize &&
                        file.read(display.getFrameBuffer(), bufferSize) == static_cast<int>(bufferSize);
  const bool closed = file.close();
  Storage.remove(path);
  return closed && complete;
}

static uint32_t customWakeCrc(uint32_t page, uint32_t oldImage) {
  return page ^ ((oldImage << 1) | (oldImage >> 31));
}

static bool showSavedReaderPage(const x3_wake::Snapshot& snapshot) {
  wake_refresh_trace::event("preview.attempt", static_cast<int>(snapshot.page.transition));
  if (snapshot.page.transition == reader_refresh::WakeTransition::CustomMono) {
    if (!BoardConfig::isX4Pro() || display.getController() != HalDisplay::Controller::UC8279 ||
        BoardConfig::ACTIVE.displayControllerVariant != 0x68) {
      wake_refresh_trace::event("custom.reject.controller");
      return false;
    }
    if (!x3_wake::matchesReaderData(snapshot)) {
      wake_refresh_trace::event("custom.reject.reader_data");
      return false;
    }
    if (!loadSleepFrameBuffer(CUSTOM_WAKE_BASE_FILE)) {
      wake_refresh_trace::event("custom.reject.reference_file");
      return false;
    }

    wake_refresh_trace::event("custom.reference.loaded");
    const uint32_t oldImageCrc = wakeFrameCrc();
    display.setInverted(false);
    // Seed the controller with the pixels already on the glass, without an
    // activation. Only paint after both saved frames have passed validation.
    renderer.cleanupGrayscaleWithFrameBuffer();
    if (!loadSleepFrameBuffer(COVER_WAKE_FRAME_FILE) ||
        customWakeCrc(wakeFrameCrc(), oldImageCrc) != snapshot.frameCrc) {
      wake_refresh_trace::event("custom.frames.rejected");
      return false;
    }
    wake_refresh_trace::event("custom.frames.accepted");
    renderer.setFadingFix(snapshot.fadingFix);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    boot_timing::mark(boot_timing::SavedPageVisible);
    return true;
  }
  const bool moon = snapshot.page.transition == reader_refresh::WakeTransition::Moon;
  if (!loadSleepFrameBuffer(moon ? SLEEP_FRAME_FILE : COVER_WAKE_FRAME_FILE) ||
      wakeFrameCrc() != snapshot.frameCrc) {
    wake_refresh_trace::event("preview.reject.frame");
    LOG_ERR("SLP", "Reader wake frame missing or corrupt");
    return false;
  }
  display.setInverted(false);
  renderer.setFadingFix(snapshot.fadingFix);
  if (moon) {
    // The glass still shows the moon. Restore that baseline in the controller,
    // replace the moon pixels in the host buffer, then update differentially.
    renderer.cleanupGrayscaleWithFrameBuffer();
    ReaderUtils::applyOrientation(renderer, snapshot.page.orientation);
    const bool restored = SleepActivity::copyMoonBackground(renderer, wakeMoonBackground, true);
    renderer.setOrientation(GfxRenderer::Portrait);
    if (!restored) return false;
    if (BoardConfig::isX4Pro() && display.getController() == HalDisplay::Controller::UC8279 &&
        BoardConfig::ACTIVE.displayControllerVariant == 0x68) {
      // The validated retained page is monochrome. Complete a normal B/W
      // differential refresh so subsequent Magic Flash starts from that page.
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    }
  } else if ((snapshot.page.transition == reader_refresh::WakeTransition::ProCoverFast ||
              snapshot.page.transition == reader_refresh::WakeTransition::ProCustomGray) &&
             BoardConfig::isX4Pro() && display.getController() == HalDisplay::Controller::UC8279 &&
             BoardConfig::ACTIVE.displayControllerVariant == 0x68) {
    // On wake there is no trustworthy B/W shadow of the gray cover. Force
    // every pixel to take a differential transition with the opposite target
    // in OLD RAM, and let the completed refresh commit the real text baseline.
    wake_refresh_trace::event(snapshot.page.transition == reader_refresh::WakeTransition::ProCoverFast
                                  ? "preview.cover.fast" : "preview.custom.gray.fast");
    renderer.invertScreen();
    renderer.cleanupGrayscaleWithFrameBuffer();
    renderer.invertScreen();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    wake_refresh_trace::event("preview.cover.half");
    renderer.restorePageFromSleepCover();
  }
  boot_timing::mark(boot_timing::SavedPageVisible);
  return true;
}

// Plugin-event delivery on the way into deep sleep. sleep.enter is delivered
// now — over the live connection, or by bringing WiFi up when a plugin
// subscribes (e.g. fetching a fresh /sleep.bmp so THIS sleep shows it — the
// drain runs before goToSleep() renders the sleep screen). The connect path
// is bounded (join deadline + drain event budget), skipped on low battery,
// and sleep is never blocked on the network: a failed join or delivery just
// sleeps with the previous image and the queued events retry on the next
// drain (at-least-once). The caller's WiFi shutdown tears the radio down
// either way. Deferrable events already queued (reader.exit) ride along in
// the same drain.
static void deliverSleepPluginEvents(const bool allowNetwork) {
  // Activity-owned state must be queued before sleep.enter and before this
  // same-sleep drain. The hook is idempotent with ordinary activity teardown.
  activityManager.prepareForSleep();

  // Sleeping straight out of a book is the common flow, but the reader's own
  // reader.exit only fires later, inside goToSleep() — after this drain. Carry
  // the book and progress on sleep.enter itself so a sync handler bound to it
  // pushes current progress on THIS connection, not the next one.
  pluginevents::Var vars[3];
  size_t varCount = 0;
  char percent[8];
  char endPercent[8];
  const ScreenshotInfo info = activityManager.getScreenshotInfo();
  if (info.readerType != ScreenshotInfo::ReaderType::None && !APP_STATE.openEpubPath.empty()) {
    snprintf(percent, sizeof(percent), "%d", info.progressPercent);
    snprintf(endPercent, sizeof(endPercent), "%d", info.endProgressPercent);
    vars[varCount++] = {"book", APP_STATE.openEpubPath.c_str()};
    vars[varCount++] = {"percent", percent};
    vars[varCount++] = {"end_percent", endPercent};
  }
  pluginevents::emit(pluginevents::Event::SleepEnter, vars, varCount);
  if (!allowNetwork) return;
  if (WiFi.status() == WL_CONNECTED) {
    pluginevents::drain(&renderer);
    return;
  }
  // Any connect-flagged queued event justifies the join, not only
  // sleep.enter: reader.session is queued while reading and delivered on this
  // same sleep, and a progress-sync plugin usually subscribes to it alone.
  if (!pluginevents::wantsConnectAny()) return;
  if (powerManager.getBatteryPercentage() < 20) return;
  // Sleep can be the first WiFi use since boot or deep-sleep wake.
  WIFI_STORE.loadFromFile();
  const auto cred = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!cred) return;

  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  const unsigned long joinDeadline = millis() + 10000;
  while (WiFi.status() != WL_CONNECTED && millis() < joinDeadline) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    trustedtime::startSync();  // snap the clock floor while the network is up
    pluginevents::drain(&renderer);
  } else {
    LOG_DBG("MAIN", "Sleep-event WiFi join timed out; deferring delivery");
  }
}

#if FREEINK_DEVICE_PICCO
// Charge strip along the bottom of the retained sleep screen.
static void drawSleepChargeStrip(const bool charging, const uint16_t percent) {
  RenderLock lock;
  const int h = renderer.getLineHeight(SMALL_FONT_ID) + 12;
  const int y = renderer.getScreenHeight() - h;
  renderer.fillRect(0, y, renderer.getScreenWidth(), h, false);
  char text[48];
  snprintf(text, sizeof(text), "%s  %u%%", charging ? tr(STR_CHARGING) : tr(STR_CHARGED), percent);
  renderer.drawCenteredText(SMALL_FONT_ID, y + 6, text);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

// Shows the charge strip while the power HAL holds sleep on USB power, and
// removes it again if USB goes away. Returns true when the power button ended
// the hold.
static bool holdSleepWhileUsbPowered() {
  if (!gpio.isUsbConnected()) return false;
  // Keep the sleep frame so unplugging can remove the strip. 48 KB of PSRAM,
  // freed when the hold ends.
  const size_t frameSize = renderer.getBufferSize();
  auto* const sleepFrame = static_cast<uint8_t*>(heap_caps_malloc(frameSize, MALLOC_CAP_SPIRAM));
  if (sleepFrame) memcpy(sleepFrame, renderer.getFrameBuffer(), frameSize);
  const bool buttonPressed = powerManager.holdWhileUsbPowered(gpio, drawSleepChargeStrip);
  if (sleepFrame) {
    if (!buttonPressed) {
      RenderLock lock;
      memcpy(renderer.getFrameBuffer(), sleepFrame, frameSize);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
    heap_caps_free(sleepFrame);
  }
  return buttonPressed;
}
#endif

// Enter deep sleep mode
void enterDeepSleep(bool fromTimeout = false) {
  wake_refresh_trace::begin("sleep");
  wake_refresh_trace::event("sleep.settings", fromTimeout, SETTINGS.sleepScreen, SETTINGS.sleepScreenCoverFilter);
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
#if defined(YACP_PERSONAL_GO)
  if (!GoDgs::maintenanceFrame()) {
    GoDgs::markManualSleep();
  }
  if (!GoDgs::maintenanceFrame())
#endif
    APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();
  APP_STATE.sleepFrameInverted = display.isInverted();

  const bool isQuickResumeSleep = SleepActivity::usesQuickResume(fromTimeout);
  bool sleepPopupShown = false;
  if (gpio.supportsReaderWakePreview() && !fromTimeout && !isQuickResumeSleep
#if defined(YACP_PERSONAL_GO)
      && !GoDgs::maintenanceFrame()
#endif
  ) {
    RenderLock lock;
    sleepPopupShown = SleepActivity::showPreparationPopup(renderer);
  }

  // Sleep may end in a power-off (battery death, latch); persist the clock
  // floor now so a later cold boot resumes from it.
  trustedtime::note();

  // Stop Bluetooth before plugin delivery can start WiFi; a timeout defers delivery.
  deepSleepInProgress = true;
  const bool bluetoothStopped = bleturner::beforeSleep(2000);
  if (!bluetoothStopped) LOG_ERR("SLP", "Bluetooth did not stop; deferring sleep-event delivery");
  deliverSleepPluginEvents(bluetoothStopped);

  // Every sleep mode leaves a complete retained frame on the e-ink panel. Keep
  // it visible until the first useful reader or home paint replaces it.
  APP_STATE.showBootScreen = false;
#ifdef SIMULATOR
  PowerHistory::commitBeforeSleep(gpio.isUsbConnected());
#else
  PowerHistory::commitBeforeSleep(gpio.isUsbConnectedCached());
#endif

  const bool stateSaved = APP_STATE.saveToFile();
#if defined(YACP_X3_WAKE_ONLY_TEST)
  bool wakeOnlySaved = false;
  if (gpio.deviceIsX3() && activityManager.isForegroundReader()) {
    RenderLock renderLock;
    if (!display.isInverted() && renderer.getFrameBuffer()) {
      wakeOnlySaved = saveSleepFrameBuffer(WAKE_ONLY_PENDING);
    }
  }
#endif
  const bool coverMode = SETTINGS.sleepScreen == CrossPointSettings::COVER ||
                         SETTINGS.sleepScreen == CrossPointSettings::COVER_CUSTOM;
  const bool proCoverFast = SETTINGS.sleepScreen == CrossPointSettings::COVER &&
                            BoardConfig::isX4Pro() && SETTINGS.supportsUc8279MagicFlash() && SETTINGS.isMagicFlash();
  const bool customMagicWake = BoardConfig::isX4Pro() && SETTINGS.supportsUc8279MagicFlash() &&
                               SETTINGS.isMagicFlash() && !fromTimeout && !isQuickResumeSleep &&
                               SETTINGS.sleepScreen == CrossPointSettings::CUSTOM;
  auto wakeTextPage = stateSaved && gpio.supportsReaderWakePreview() &&
                              (isQuickResumeSleep || coverMode || customMagicWake)
                          ? activityManager.retainedReaderPage()
                          : reader_refresh::WakeTextPage{};
  wake_refresh_trace::event("sleep.eligible", customMagicWake, wakeTextPage.valid, stateSaved);
  uint32_t frameCrc = 0;
  if (wakeTextPage.valid) {
    if (isQuickResumeSleep) {
      wakeTextPage.valid = SleepActivity::copyMoonBackground(renderer, wakeMoonBackground, false);
      wakeTextPage.transition = reader_refresh::WakeTransition::Moon;
    } else {
      wakeTextPage.valid = saveSleepFrameBuffer(COVER_WAKE_FRAME_FILE);
      if (wakeTextPage.valid) frameCrc = wakeFrameCrc();
      wakeTextPage.transition = customMagicWake ? reader_refresh::WakeTransition::CustomMono
                                : proCoverFast  ? reader_refresh::WakeTransition::ProCoverFast
                                                : reader_refresh::WakeTransition::Cover;
    }
  }

  const auto sleepBitmap = activityManager.goToSleep(fromTimeout, sleepPopupShown);
  if (wakeTextPage.valid && wakeTextPage.transition == reader_refresh::WakeTransition::CustomMono) {
    if (sleepBitmap == SleepBitmapRender::Grayscale) {
      // The normal Cover transition already handles a grayscale source without
      // pretending the controller has a valid monochrome copy of that image.
      wakeTextPage.transition = reader_refresh::WakeTransition::ProCustomGray;
    } else {
      wakeTextPage.valid = sleepBitmap == SleepBitmapRender::Monochrome && saveSleepFrameBuffer(CUSTOM_WAKE_BASE_FILE);
      if (wakeTextPage.valid) frameCrc = customWakeCrc(frameCrc, wakeFrameCrc());
    }
  }
  if (BookPathMove::blocked() || LegacyMigration::blocked()) {
    deepSleepInProgress = false;
    LOG_ERR("MAIN", "Sleep postponed until book move recovery");
    return;
  }

  wake_refresh_trace::event("sleep.frames", static_cast<int>(wakeTextPage.transition), wakeTextPage.valid,
                            static_cast<int>(sleepBitmap));
  if (isQuickResumeSleep) {
    const bool saved = saveSleepFrameBuffer(SLEEP_FRAME_FILE);
    wakeTextPage.valid = saved && wakeTextPage.valid;
    if (wakeTextPage.valid) frameCrc = wakeFrameCrc();
  } else {
    if (Storage.exists(SLEEP_FRAME_FILE)) Storage.remove(SLEEP_FRAME_FILE);
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  // Reader onExit has committed its statistics. Preserve YACP's X3-only,
  // date-qualified backup policy without adding work to page turns.
  if (gpio.deviceIsX3() && SETTINGS.autoBackupStats && Storage.exists("/.crosspoint/global_stats.bin")) {
    ReadingStatsDateTime now;
    if (getCurrentLocalReadingStatsDateTime(now) && !backupGlobalStats(false)) {
      LOG_ERR("MAIN", "Automatic reading-statistics backup failed before sleep");
    }
  }
  halTiltSensor.deepSleep();
  display.deepSleep();
#if defined(YACP_X3_WAKE_ONLY_TEST)
  if (wakeOnlySaved && !Storage.rename(WAKE_ONLY_PENDING, WAKE_ONLY_FRAME)) {
    LOG_ERR("WAKETEST", "Could not arm the wake test");
  }
#endif
  const bool savedWakeRecord = wakeTextPage.valid &&
      x3_wake::save({wakeTextPage, frameCrc, 0, SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP,
                    SETTINGS.fadingFix != 0},
                   wakeMoonBackground);
  wake_refresh_trace::event("sleep.record", savedWakeRecord);
  wake_refresh_trace::save("/yacp-sleep-trace.txt");
  if (wakeTextPage.valid && !savedWakeRecord) {
    LOG_ERR("SLP", "Could not save reader wake record; the next wake will render normally");
  }
#if defined(YACP_PERSONAL_GO)
  GoDgs::armNextPoll();
#endif
#if defined(YACP_PERSONAL_GO_TIMER_WAKE)
  const bool timerWakeSleep = GoDgs::timerWakeSleepPending();
#endif
  Storage.prepareForDeepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

#if defined(YACP_PERSONAL_GO_TIMER_WAKE)
  if (timerWakeSleep) powerManager.startTimerWakeDeepSleep(gpio);
#endif
  powerManager.startDeepSleep(gpio);
}

static void initializeDisplay(bool seamless) {
#if !FREEINK_MCU_C3
  // C3 resolves its controller in HalGPIO::begin() before SPI claims the
  // display pins. X4 Pro skips that C3-only path, so probe here before
  // display.begin() selects and initializes its panel driver.
  static bool controllerResolved = false;
  if (!controllerResolved) {
    controllerResolved = true;
    if (freeink::applyXteinkDisplayController()) {
      LOG_DBG("MAIN", "Panel controller: UltraChip UC81xx variant detected");
    }
  }
#endif
  display.begin(seamless);
  renderer.begin();
}

void setupDisplayAndFonts(bool seamless = false, bool alreadyInitialized = false) {
  if (!alreadyInitialized) {
    initializeDisplay(seamless);
  } else if (!seamless) {
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  }
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  registerYacpFonts(renderer);
#ifndef OMIT_FONTS

  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  wake_refresh_trace::begin("wake");
  boot_timing::started = millis();
  BoardConfig::holdPowerRails();

#ifdef ENABLE_SERIAL_LOG
#ifdef CROSSPOINT_WAIT_FOR_USB_SERIAL
  // Development builds preserve reliable early CDC logs; release builds let
  // enumeration proceed asynchronously so users do not pay this startup cost.
  delay(250);
#endif
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

#if defined(YACP_X3_WAKE_ONLY_TEST)
  gpio.begin();
  const auto testGpioReady = millis();
  const auto reset = esp_reset_reason();
  if (gpio.deviceIsX3() && (reset == ESP_RST_POWERON || reset == ESP_RST_DEEPSLEEP) && Storage.begin()) {
#if YACP_X3_WAKE_ONLY_TEST == 2
    if (showWakeOnlyPage(testGpioReady, millis())) renderer.begin();
#else
    if (showWakeOnlyPage(testGpioReady, millis())) return;
#endif
  }
#endif

  HalSystem::begin();
  // checkPanic() clears the watchdog capture marker after a successful SD
  // dump, so retain the boot classification for the later activity route.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_MAX) ? silentRebootTarget : 0;
  const bool silentRebootLightOn = isSilentReboot && (silentRebootPayload & SILENT_REBOOT_LIGHT_ON) != 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;
  silentRebootPayload = 0;

#if !defined(YACP_X3_WAKE_ONLY_TEST)
  gpio.begin();
#endif
  powerManager.begin();

  // Board TX-power ceiling. It can only be set once WiFi has started, so it is
  // applied on every STA/AP start rather than at each place that brings WiFi up.
  if (BoardConfig::ACTIVE.wifiMaxTxPower != 0) {
    const auto applyTxCap = [](arduino_event_id_t, arduino_event_info_t) {
      const esp_err_t err = esp_wifi_set_max_tx_power(static_cast<int8_t>(BoardConfig::ACTIVE.wifiMaxTxPower));
      if (err != ESP_OK) LOG_ERR("WIFI", "TX power cap failed: %d", err);
    };
    WiFi.onEvent(applyTxCap, ARDUINO_EVENT_WIFI_STA_START);
    WiFi.onEvent(applyTxCap, ARDUINO_EVENT_WIFI_AP_START);
  }

  const auto wakeupReason = gpio.getWakeupReason();
  boot_timing::mark(boot_timing::Hardware);
  auto bootShortcut = boot_shortcut::Action::None;
#if FREEINK_MCU_C3 && !defined(SIMULATOR)
  static_assert(HalGPIO::BTN_BACK == 0 && HalGPIO::BTN_UP == 4 && HalGPIO::BTN_DOWN == 5);
  if (!isSilentReboot
#if YACP_X3_WAKE_ONLY_TEST == 2
      && !wakeOnlyDisplayed
#endif
  ) {
    const uint8_t first = gpio.samplePhysicalLadderButtons();
    delay(8);
    bootShortcut = boot_shortcut::forBoot(first, gpio.samplePhysicalLadderButtons(),
                                          wakeupReason == HalGPIO::WakeupReason::PowerButton, isSilentReboot);
  }
#endif
  // Sample the wake hold now — a click wake is released within milliseconds of
  // boot — but defer the sleep-or-boot decision until SETTINGS is loaded below:
  // click-to-wake is a setting, and an X4 battery power-off cuts all power, so
  // only SD state survives to the next boot.
  // The Metalio boots after the button is released, so it skips the check.
  const bool wakeHoldVerified =
#if YACP_X3_WAKE_ONLY_TEST == 2
      wakeOnlyDisplayed ||
#endif
      wakeupReason != HalGPIO::WakeupReason::PowerButton || BoardConfig::isMetalioEInk4() ||
                                gpio.verifyPowerButtonWakeup();

  // X4 Pro and X4 Classic both map BTN_UP to GPIO0 — an ESP32-S3 boot strap — so
  // gate recovery on the non-strap Down key (GPIO7) to avoid a stuck-in-recovery loop.
#if FREEINK_MCU_C3 && !defined(SIMULATOR)
  const bool recoveryFirmwareMode = bootShortcut == boot_shortcut::Action::FirmwareUpdate;
#else
  const auto recoveryButton = (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? MappedInputManager::Button::Down
                                                                                     : MappedInputManager::Button::Up;
  // The Picco has no second app slot to flash into (partitions_picco.csv).
  const bool recoveryFirmwareMode = wakeupReason == HalGPIO::WakeupReason::PowerButton && !BoardConfig::isPaperMono() &&
                                    mappedInputManager.isPressed(recoveryButton);
#endif

#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");
#else
  LOG_INF("MAIN", "Device: %s", BoardConfig::ACTIVE.name);
#endif

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
#if defined(YACP_X3_WAKE_ONLY_TEST)
  if (!Storage.ready() && !Storage.begin()) {
#else
  if (!Storage.begin()) {
#endif
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }
  boot_timing::mark(boot_timing::SdMount);
  const bool isSleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  wake_refresh_trace::event("wake.cause", static_cast<int>(wakeupReason), wakeHoldVerified);
  const bool allowPreview =
      isSleepWake && !isSilentReboot && !rebootedFromPanic && bootShortcut == boot_shortcut::Action::None &&
      !recoveryFirmwareMode;
  auto savedPage =
      gpio.supportsReaderWakePreview() ? x3_wake::consume(allowPreview, wakeMoonBackground) : x3_wake::Snapshot{};
  wake_refresh_trace::event("wake.record", savedPage.page.valid, static_cast<int>(savedPage.page.transition),
                            savedPage.clickToWake);
#if defined(YACP_PERSONAL_GO)
  // Log before settings and migration so a timer wake that stops early is
  // distinguishable from a wake that never reached firmware setup.
  GoDgs::ActivityLog::append("boot", "wake", 0, 0, static_cast<int32_t>(esp_sleep_get_wakeup_cause()),
                             static_cast<uint32_t>(esp_reset_reason()));
#endif

  ButtonNavigator::setMappedInputManager(mappedInputManager);
  if (recoveryFirmwareMode) {
    // Recovery must remain available even when migration is paused. It must
    // never load/resave the user's settings or reader state.
    setupDisplayAndFonts(false);
    auto recovery = makeUniqueNoThrow<SdFirmwareUpdateActivity>(renderer, mappedInputManager, true);
    if (recovery)
      activityManager.replaceActivity(std::move(recovery));
    else
      activityManager.goToFullScreenMessage(tr(STR_ERROR_GENERAL_FAILURE));
    return;
  }

  // This is a display-only preview of this device's last committed reading
  // frame. A battery wake is POWERON and must still take the migration checks
  // for a cold boot before opening or writing any reading state.
  bool displayInitialized = false;
#if YACP_X3_WAKE_ONLY_TEST == 2
  displayInitialized = wakeOnlyDisplayed;
  if (wakeOnlyDisplayed) {
    // Adopt the fast-painted pixels only if they match the validated cover
    // snapshot. Otherwise let the normal reader repaint the correct page.
    if (!savedPage.page.valid || savedPage.page.transition != reader_refresh::WakeTransition::Cover ||
        wakeFrameCrc() != savedPage.frameCrc || BookPathMove::hasPending() ||
        !LegacyMigration::canPreviewSavedReaderPage() || !x3_wake::matchesReaderData(savedPage)) {
      savedPage.page = {};
    }
  } else
#endif
  if (savedPage.page.valid && (wakeHoldVerified || savedPage.clickToWake) &&
      !BookPathMove::hasPending() && LegacyMigration::canPreviewSavedReaderPage()) {
    initializeDisplay(true);
    displayInitialized = true;
    readerWakePreviewDisplayed = showSavedReaderPage(savedPage);
    wake_refresh_trace::event("preview.result", readerWakePreviewDisplayed);
    if (readerWakePreviewDisplayed) {
      startWakeTurnCapture();
    } else {
      savedPage.page = {};
    }
  } else {
    savedPage.page = {};
  }
  halTiltSensor.begin();
  halClock.begin();

  // Migration precedes every state/settings loader, including their resaves.
#ifdef SIMULATOR
  const bool checkedSleepResume = false;
#else
  // A normal deep-sleep wake keeps the same firmware and checked migration.
  const bool checkedSleepResume = esp_reset_reason() == ESP_RST_DEEPSLEEP;
#endif
  if (!LegacyMigration::run(checkedSleepResume)) {
    setupDisplayAndFonts(false, displayInitialized);
    activityManager.goToFullScreenMessage(
        LegacyMigration::sourceBackupFailed() ? tr(STR_BACKUP_STATS_FAILED) : tr(STR_ERROR_GENERAL_FAILURE));
    return;
  }
  HalSystem::checkPanic();
  boot_timing::mark(boot_timing::DataCheck);

  const bool pendingBookMove = BookPathMove::hasPending();
  const bool moveSnapshotsReady = !pendingBookMove || BookPathMove::prepareRecovery();
  if (!moveSnapshotsReady) {
    SETTINGS.loadFromFile();
    I18N.setLanguage(static_cast<Language>(SETTINGS.language));
    setupDisplayAndFonts(false, displayInitialized);
    activityManager.goToFullScreenMessage(tr(STR_ERROR_GENERAL_FAILURE));
    LOG_ERR("MAIN", "Move metadata recovery blocked; no state loaded or saved");
    return;
  }
  auto wakeTextPage = savedPage.page.valid && !pendingBookMove && x3_wake::matchesReaderData(savedPage)
                          ? savedPage.page
                          : reader_refresh::WakeTextPage{};
  APP_STATE.loadFromFile();
  PowerHistory::begin(gpio.isUsbConnected());
  // The early paint already established a monochrome panel baseline. Use it
  // for a differential redraw even if we cannot adopt the cached reader page.
  const bool hasWakeFrame = wakeTextPage.valid || readerWakePreviewDisplayed;
  const bool isPersistedSleepWake =
      isSleepWake && !APP_STATE.showBootScreen && (!gpio.deviceIsX3() || checkedSleepResume || hasWakeFrame);

  if (recoveryFirmwareMode) {
    LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)",
            (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? "DOWN" : "UP");
  }

  // Touch boards default the reader menu to the toolbar overlay instead of the
  // full-screen list. Seeded before the load: fromJson() falls back to the
  // in-memory value only when the file carries no readerMenuStyle key, so a
  // user's saved choice (either style) still wins.
  if (gpio.hasTouch()) {
    SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  }
  const bool settingsLoaded = SETTINGS.loadFromFile();
  if (!settingsLoaded && gpio.deviceIsX3() && !Storage.exists(CrossPointSettings::getFilePath())) {
    // CrossPoint's X3 no-flash reinforcement is YACP's Magic Flash behavior.
    // Existing files, including migrated settings, always retain the user's choices.
    // Keep the Classic cadence and smoothing preferences for a later mode
    // switch. Magic Flash imposes its effective values without erasing them.
    SETTINGS.refreshAction = CrossPointSettings::REFRESH_BW_REINFORCEMENT_2X;
    if (!SETTINGS.saveToFile()) {
      LOG_ERR("MAIN", "Failed to save first X3 settings defaults");
    }
  }
  // Push the saved timezone's POSIX rule into the clock (migrating the legacy
  // UTC-offset setting on first boot after the update).
  timezones::applyToClock();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  UITheme::getInstance().reload();
  if (pendingBookMove && !BookPathMove::recover()) {
    setupDisplayAndFonts(false, displayInitialized);
    activityManager.goToFullScreenMessage(tr(STR_ERROR_GENERAL_FAILURE));
    LOG_ERR("MAIN", "Book move recovery blocked; repair SD state or restart to retry");
    return;
  }
  if (pendingBookMove && !LegacyMigration::repairTextAfterBookMove()) {
    setupDisplayAndFonts(false, displayInitialized);
    activityManager.goToFullScreenMessage(tr(STR_ERROR_GENERAL_FAILURE));
    LOG_ERR("MAIN", "TXT position conversion blocked after book move");
    return;
  }

#if defined(YACP_PERSONAL_GO)
  if (GoDgs::isTimerWake()) {
    if (GoDgs::beginScheduledPoll()) {
      GoDgs::pollOnce();
      GoDgs::finishScheduledPoll();
    }
    GoDgs::setMaintenanceFrame(true);
    setupDisplayAndFonts(true);
    enterDeepSleep();
    return;
  }
#endif

  // Brightness and warmth are always restored. A normal wake starts with the
  // light off unless Restore Light on Wake is enabled; silent maintenance
  // reboots replay the live state captured at restart, so they neither go dark
  // nor light up against the user's wake preference. A slide switch bound to
  // the light decides it from its position instead.
  const bool restoreLightOn = isSilentReboot ? silentRebootLightOn
                              : slideSwitchOwnsLight()
                                  ? gpio.isToggleSwitchOn()
                                  : (SETTINGS.frontlightOn != 0 && SETTINGS.frontlightRestoreOnWake != 0);
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth, restoreLightOn);

  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      // With Short Power Button Press = Sleep, a single click wakes on any
      // device; otherwise the button must still be held (ghost-wake debounce).
      // Picco: plugging in USB pulses the wake line; power on, as stock does.
      if (!wakeHoldVerified && SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::SLEEP &&
          !(FREEINK_DEVICE_PICCO && gpio.isUsbConnected())) {
        LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
        Storage.prepareForDeepSleep();
        powerManager.startDeepSleep(gpio);
      }
      wakePowerReleasePending = true;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // Most devices return to sleep after a USB-powered cold boot.
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_X4CLASSIC || FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_EEGO_A4 || \
    FREEINK_DEVICE_METALIO_EINK4 || FREEINK_DEVICE_PICCO
      // X4 Pro must stay awake so USB Serial/JTAG remains available after leaving
      // USB Drive and reconnecting the cable. Paper Mono has no armable GPIO wake
      // (its button is behind the PMIC). Metalio also needs native USB available
      // after a USB-powered boot, as does the Picco. EEGO A4's post-flash reset reads as
      // POWERON (native-USB), so a flash would otherwise be misclassified as a
      // USB-power cold boot and sleep. Sleeping any of these here would strand
      // the device in a USB-replug boot loop (or sleep right after a flash).
      break;
#else
      Storage.prepareForDeepSleep();
      powerManager.startDeepSleep(gpio);
      break;
#endif
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Apply a switch moved during sleep before the first paint, so the wake
  // screen does not draw in the old night mode and then repaint.
  handleSlideSwitch();

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  // A battery-powered X3 relies on the validated SD snapshot rather than RTC.
  const BootResume resume = bootShortcut == boot_shortcut::Action::SafeHome ? BootResume::Splash
                            : isSilentReboot                                ? BootResume::Silent
                            : isPersistedSleepWake                          ? BootResume::SplashlessWake
                                                                            : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;
  bool needsWakeRefresh = false;

#if defined(YACP_PERSONAL_GO)
  const bool resumeGo = isPersistedSleepWake && GoDgs::hasPlayablePending();
#endif
  std::string bookToResume;
  if (bootShortcut == boot_shortcut::Action::None && !recoveryFirmwareMode && !rebootedFromPanic &&
      resume != BootResume::Silent &&
#if defined(YACP_PERSONAL_GO)
      !resumeGo &&
#endif
      APP_STATE.lastSleepFromReader && !APP_STATE.openEpubPath.empty() && APP_STATE.readerActivityLoadCount == 0 &&
      !mappedInputManager.isPressed(MappedInputManager::Button::Back)) {
    bookToResume.swap(APP_STATE.openEpubPath);
    ++APP_STATE.readerActivityLoadCount;
  }
  if (resume == BootResume::SplashlessWake || !bookToResume.empty()) {
    // Persist both guards together, before touching the retained frame or opening a book.
    APP_STATE.showBootScreen = true;
    if (!APP_STATE.saveToFile() && !bookToResume.empty()) {
      LOG_ERR("BOOT", "Could not save resume guard; opening Home");
      APP_STATE.openEpubPath.swap(bookToResume);
      --APP_STATE.readerActivityLoadCount;
    }
  }

  boot_timing::mark(boot_timing::Stores);
  wake_refresh_trace::event("wake.route", static_cast<int>(resume), displayInitialized, readerWakePreviewDisplayed);
  setupDisplayAndFonts(resume != BootResume::Splash, displayInitialized);
  boot_timing::mark(boot_timing::Display);

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::SplashlessWake: {
      if (wakeTextPage.valid && !bookToResume.empty() &&
          wakeTextPage.book == std::hash<std::string>{}(bookToResume)) {
        allowFastInitialReaderRefresh = true;
        break;
      }
      wakeTextPage = {};
      if (readerWakePreviewDisplayed && !bookToResume.empty() && !pendingBookMove && !SETTINGS.screenInverted &&
          !SETTINGS.readerDarkMode) {
        allowFastInitialReaderRefresh = true;
        break;
      }
      if (displayInitialized) {
        wake_refresh_trace::event("wake.promote.full");
        renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
        needsWakeRefresh = true;
      } else if (loadSleepFrameBuffer(SLEEP_FRAME_FILE)) {
        display.setInverted(APP_STATE.sleepFrameInverted);
        const bool proMagicWake = BoardConfig::isX4Pro() && SETTINGS.isMagicFlash() &&
                                  APP_STATE.lastSleepFromReader && !display.isInverted();
        if (gpio.deviceIsX3() || proMagicWake) {
          renderer.cleanupGrayscaleWithFrameBuffer();
          allowFastInitialReaderRefresh = true;
        }
      } else {
        needsWakeRefresh = true;
      }
      break;
    }
    case BootResume::Splash:
      // On a normal power-on the first useful screen can do the initial panel
      // sync. Keep the startup indicator on recovery and non-power-on routes.
#ifndef SIMULATOR
      if (esp_reset_reason() != ESP_RST_POWERON || bootShortcut != boot_shortcut::Action::None || rebootedFromPanic)
#endif
        activityManager.goToBoot();
      break;
  }

  // ActivityManager resolves subsequent reader/menu polarity on each render.
  boot_timing::mark(boot_timing::ResumeFrame);

  if (bootShortcut == boot_shortcut::Action::SafeHome) {
    LOG_INF("MAIN", "Physical BACK boot shortcut: safe Home");
    activityManager.goHome();
  } else if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (bootShortcut == boot_shortcut::Action::JoinNetwork) {
    LOG_INF("MAIN", "Physical DOWN boot shortcut: Join Network");
    // Activity ownership requires heap storage, just like the ordinary file-transfer route.
    auto activity = makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInputManager, true);
    if (activity) {
      activityManager.replaceActivity(std::move(activity));
    } else {
      LOG_ERR("MAIN", "Cannot allocate network boot activity; returning Home");
      activityManager.goHome();
    }
  } else if (rebootedFromPanic) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
#if defined(YACP_PERSONAL_GO)
  } else if (resumeGo) {
    auto go = GoDgs::makeGoActivity(renderer, mappedInputManager);
    if (go)
      activityManager.replaceActivity(std::move(go));
    else
      activityManager.goHome();
#endif
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_JOIN_NETWORK) {
    // Rebooted on the way *into* File Transfer > Join Network for a fresh heap;
    // resume that flow directly instead of landing on home.
    activityManager.goToJoinNetwork();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_SETTINGS) {
    // Back out of the WiFi rows and the user is where they left off, not on Home.
    activityManager.goToSettings(SettingsSection::Network);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (bookToResume.empty()) {
    wake_refresh_trace::event("wake.home", APP_STATE.lastSleepFromReader, APP_STATE.readerActivityLoadCount,
                              APP_STATE.openEpubPath.empty());
    activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
  } else {
    const auto retainedPage = resume == BootResume::SplashlessWake ? wakeTextPage : reader_refresh::WakeTextPage{};
    wake_refresh_trace::event("wake.reader", allowFastInitialReaderRefresh, retainedPage.valid);
    activityManager.goToReader(std::move(bookToResume), allowFastInitialReaderRefresh, retainedPage);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  if (bootShortcut != boot_shortcut::Action::None) {
    gpio.update();
    delay(10);
    gpio.update();
    mappedInputManager.waitForButtonReleaseAfterRemap();
  }

  allowSleepAt = millis() + 2000;
  boot_timing::mark(boot_timing::Routed);
}

void loop() {
#if YACP_X3_WAKE_ONLY_TEST == 1
  if (wakeOnlyDisplayed) {
    gpio.update();
    if (gpio.wasPressed(HalGPIO::BTN_BACK)) ESP.restart();
    delay(10);
    return;
  }
#endif
  if (readerWakePreviewDisplayed) captureWakeTurn.store(false, std::memory_order_release);
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  mappedInputManager.update();

  if (BookPathMove::blocked() || LegacyMigration::blocked()) {
    // Do not open books, serve writes or save state over an interrupted move.
    // A deliberate Power hold retries recovery on the next boot.
    if (mappedInputManager.wasLongPressed(MappedInputManager::Button::Power, 2000)) {
      LOG_INF("MAIN", "User requested restart to retry storage recovery");
      ESP.restart();
    }
    activityManager.loop();
    delay(50);
    return;
  }

  static bool readerPowerConsumed = false;
  // Consume the release even if the command has already opened another screen.
  if (readerPowerConsumed && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    readerPowerConsumed = false;
    return;
  }

  // Bluetooth page turner, before the USB drive's early return: a card handed to USB stops the radio
  // before anything else.
  bleturner::Scene scene{};
  // Home counts only for builds whose radio may start there (the S3 profile, RadioPolicy.h).
  const bool homeShown = activityManager.isForegroundHomeShown();
  const bool remoteContext = activityManager.isRemoteReaderContext();
  scene.where = remoteContext ? bleturner::Where::Reader
                : homeShown   ? bleturner::Where::Home
                              : bleturner::Where::Elsewhere;
  scene.visit = activityManager.visit();
  scene.pageShown = homeShown || remoteContext;
  scene.storageBusy = activityManager.requiresExclusiveStorageLoop();
  scene.wifiOn = WiFi.getMode() != WIFI_MODE_NULL;
  const bool remoteActed = bleturner::tick(scene);

  if (activityManager.requiresExclusiveStorageLoop()) {
    // USB Drive handed the raw SD card to the host. Do not run screenshots,
    // sleep, shortcuts, or normal navigation while its filesystem is detached.
    activityManager.loop();
    if (activityManager.preventAutoSleep()) {
      powerManager.setPowerSaving(false);
      delay(10);
    } else {
      // No host is active, so a slower loop is safe. The activity itself times
      // out the raw-storage handoff rather than entering deep sleep detached.
      powerManager.setPowerSaving(true);
      delay(50);
    }
    return;
  }

  // After the exclusive-storage return: it saves settings to the SD card.
  handleSlideSwitch();
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

  // Preserve the saved C3 preference without applying it to S3 OTP panels,
  // matching the existing SettingsActivity board gate.
  renderer.setFadingFix(SETTINGS.fadingFix && !BoardConfig::isX4Pro() && !BoardConfig::isX4Classic());

  // The ROM console does not depend on Arduino USB CDC's connection state.
  if ((Serial || FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF) && millis() - lastMemPrint >= 10000) {
    const auto heap = HalMemory::getInternalHeap();
    LOG_INF("MEM", "Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes", heap.freeBytes,
            heap.totalBytes, heap.minFreeBytes, heap.largestBlockBytes);
#ifdef BOARD_HAS_PSRAM
    const auto psram = HalMemory::getPsramHeap();
    LOG_INF("MEM", "PSRAM: Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes",
            psram.freeBytes, psram.totalBytes, psram.minFreeBytes, psram.largestBlockBytes);
#endif
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "BOOT_TIMING") {
        static constexpr const char* phaseNames[] = {"hardware", "sd_mount", "saved_page_visible", "data_check",
                                                     "stores", "display", "resume_frame", "routed", "first_paint",
                                                     "input_loop"};
        static_assert(sizeof(phaseNames) / sizeof(phaseNames[0]) == boot_timing::PhaseCount);
        logSerial.printf("BOOT_TIMING_MS_FROM_SETUP retained_page=%u\n",
                         static_cast<unsigned>(boot_timing::retainedPage.load(std::memory_order_relaxed)));
        for (size_t i = 0; i < boot_timing::PhaseCount; ++i) {
          const auto stamp = boot_timing::stamps[i].load(std::memory_order_acquire);
          if (stamp) logSerial.printf("%s=%lu\n", phaseNames[i], static_cast<unsigned long>(stamp - 1));
        }
      } else if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() || halTiltSensor.hadActivity() ||
      remoteActed || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  // Let wake continue as soon as its hold has been verified. The release can
  // arrive after setup, so consume that one input frame rather than making it
  // a page turn, refresh, or other short power-button action.
  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    // Keep both keys owned by the chord until both are up. Otherwise a later
    // Down release (or its still-held state) can become a reader page turn.
    if (gpio.isPressed(HalGPIO::BTN_POWER) || gpio.isPressed(HalGPIO::BTN_DOWN)) return;
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
    return;
  }

  // Consume the second X4 Pro power-button release so it does not also run a
  // configured short-power action after toggling the frontlight.
  if (handleX4ProFrontlightDoubleClick()) {
    return;
  }

  const bool x4ProDoubleClickPwrLight = BoardConfig::isX4Pro() && SETTINGS.doubleClickPwrLight;

#if FREEINK_CAP_TOUCH
  // A single X4 Pro power click becomes Confirm only after the frontlight
  // double-click window expires without a second click.
  mappedInputManager.setPowerConfirmClickFrame(false);
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM && x4ProDoubleClickPwrLight) {
    if (lastX4ProPowerClickAt != 0 && millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
      lastX4ProPowerClickAt = 0;
      mappedInputManager.setPowerConfirmClickFrame(true);
    }
    // A release held too long to be a double-click candidate (but still within
    // the normal Confirm press duration) never reaches handleX4ProFrontlightDoubleClick's
    // click tracking above, so it needs its own Confirm check here.
    if (mappedInputManager.wasReleased(MappedInputManager::Button::Power) &&
        gpio.getPowerButtonHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS &&
        gpio.getPowerButtonHeldTime() <= SETTINGS.getPowerButtonDuration()) {
      mappedInputManager.setPowerConfirmClickFrame(true);
    }
  }
#endif

  // Same deferral for SLEEP: getPowerButtonDuration() drops to 10ms so a quick
  // tap sleeps the device, which otherwise fires on button-down and never lets
  // a second click land. Sleep only once the double-click window has passed.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP && x4ProDoubleClickPwrLight &&
      !gpio.isPressed(HalGPIO::BTN_POWER) && lastX4ProPowerClickAt != 0 &&
      millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = 0;
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (activityManager.takeReaderSleepRequest() || mappedInputManager.homeButtonAction() == HomeButtonAction::Sleep) {
    enterDeepSleep();
    return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // A hold that woke the device must be released before it can count as a new
  // in-app long press. Otherwise a user who keeps holding after wake would put
  // the device straight back to sleep once allowSleepAt expires.
  static bool powerReleasedSinceWake = false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER)) powerReleasedSinceWake = true;

  bool readerPowerOverridesSleep = false;
  const uint8_t readerLongPower = activityManager.readerPowerAction(true);
  const uint8_t readerShortPower = activityManager.readerPowerAction(false);
  const bool readerCustomPower = readerLongPower != 23 || readerShortPower != 23;
  readerPowerOverridesSleep = readerPowerConsumed || readerCustomPower;
  if (!readerPowerConsumed && readerCustomPower && powerReleasedSinceWake && millis() >= allowSleepAt) {
    const bool released = mappedInputManager.wasReleased(MappedInputManager::Button::Power);
    const bool held = gpio.isPressed(HalGPIO::BTN_POWER);
    if ((held || released) && gpio.getPowerButtonHeldTime() >= 400) {
      if (readerLongPower == 23) {
        enterDeepSleep();
        return;
      }
      lastX4ProPowerClickAt = 0;
      activityManager.queueReaderAction(readerLongPower);
      readerPowerConsumed = held;
      // A release observed after a busy render still dispatches the long action once.
      if (released) {
        activityManager.loop();
        return;
      }
    } else if (released) {
      if (readerShortPower != 23) {
        activityManager.queueReaderAction(readerShortPower);
        activityManager.loop();
        return;
      }
      if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP && !x4ProDoubleClickPwrLight) {
        enterDeepSleep();
        return;
      }
    }
  }

  // On X4 Pro with SLEEP, a press still within the click window is a
  // double-click candidate — let it be released and evaluated above instead
  // of sleeping on button-down.
  const bool x4ProAwaitingClickWindow = x4ProDoubleClickPwrLight &&
                                        SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP &&
                                        gpio.getPowerButtonHeldTime() <= X4PRO_POWER_CLICK_MAX_HOLD_MS;

  if (!readerPowerOverridesSleep && !x4ProAwaitingClickWindow && powerReleasedSinceWake && millis() >= allowSleepAt &&
      gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    LOG_DBG("MAIN", "Power button held %lums, sleeping", gpio.getPowerButtonHeldTime());
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

#if FREEINK_DEVICE_PAPERMONO
  // Paper Mono reports the PMIC power button as a one-tick click, so the held
  // path above cannot fire. With the default Ignore action, retain the normal
  // power-button meaning and shut down; explicit alternate bindings still win.
  if ((SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP ||
       SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::IGNORE) &&
      millis() >= allowSleepAt && mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    enterDeepSleep();
    return;
  }
#endif

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (mappedInputManager.homeButtonAction() == HomeButtonAction::ToggleFrontlight) {
    toggleFrontlight();
  }
  if (mappedInputManager.homeButtonAction() == HomeButtonAction::Refresh ||
      (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
       mappedInputManager.wasReleased(MappedInputManager::Button::Power))) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  // Not while reading: there a repaint is a full page re-render (visible
  // flash, the AA pass re-running, and a frontlight dip under the refresh
  // load); the reader's status bar picks the charging state up on the next
  // page turn instead.
  if (gpio.wasUsbStateChanged()) {
#ifdef SIMULATOR
    PowerHistory::noteExternalPower(gpio.isUsbConnected());
#else
    PowerHistory::noteExternalPower(gpio.isUsbConnectedCached());
#endif
    if (!activityManager.isReaderActivity()) activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  const bool firstPaintReady = boot_timing::stamps[boot_timing::FirstPaint].load(std::memory_order_acquire) != 0;
  if (readerWakePreviewDisplayed && firstPaintReady && !wakePowerReleasePending) {
    const auto physical = wakeTurnButton.load(std::memory_order_acquire);
    if (physical != 0) {
      const auto turn = mappedInputManager.sidePageClick(physical);
      if (!activityManager.isForegroundReader() || turn == RemoteButton::None || activityManager.remoteClick(turn)) {
        wakeTurnButton.store(0, std::memory_order_release);
      }
    }
  }
  activityManager.loop();
  if (firstPaintReady && !wakePowerReleasePending && !RenderLock::peek()) {
    boot_timing::mark(boot_timing::InputLoop);
  }
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  bool skipLoopDelay = false;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) {
      // Let rendering advance without treating lock contention as idle.
      delay(10);
      return;
    }
    skipLoopDelay = activityManager.skipLoopDelay();
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (skipLoopDelay) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS && !pageturner::holdsCpu.load()) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      // Sleep in short slices and wake the poll as soon as a button contact closes.
      // InputManager commits a press only when two consecutive polls agree, so a
      // press shorter than one 50 ms sleep could land in a single sample and be lost.
      const unsigned long idleStart = millis();
      while (millis() - idleStart < 50) {
        delay(10);
        if (gpio.rawInputActive()) break;
      }
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
