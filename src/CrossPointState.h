#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

struct PowerHistoryState {
  static constexpr uint8_t SAMPLE_CAPACITY = 21;

  uint32_t activeSeconds = 0;
  uint32_t cycleStartDay = 0;
  uint32_t readerPageDisplays = 0;
  uint16_t sampleActiveMinutes[SAMPLE_CAPACITY] = {};
  uint16_t sleepSamples = 0;
  uint8_t samplePercents[SAMPLE_CAPACITY] = {};
  uint8_t sampleCount = 0;
  uint8_t lastPercent = UINT8_MAX;
  uint8_t chargePendingPercent = UINT8_MAX;
  bool hasCycleStartDay = false;
  bool cycleConfirmed = false;
  bool chargePending = false;
};

static_assert(sizeof(PowerHistoryState) <= 96, "Power history must remain a small fixed-size state");

class CrossPointState {
  mutable std::mutex _mutex;

  // Static instance
  static CrossPointState instance;

 public:
  // Access the state mutex for protecting multi-field reads/writes from other cores.
  std::mutex& getMutex() const { return _mutex; }

  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  std::string favoriteSleepImagePath;
  std::string preferredSleepFolderPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};  // circular buffer of recent wallpaper indices
  uint8_t recentSleepPos = 0;                           // next write slot
  uint8_t recentSleepFill = 0;                          // valid entries (0..SLEEP_RECENT_COUNT)
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool lastSleepRenderedQuickResume = false;
  bool showBootScreen = true;
  PowerHistoryState powerHistory;

  // Returns true if idx was shown within the last checkCount picks.
  // Walks backwards from the most recently written slot.
  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
  void clearRecentSleepHistory();
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();
  uint16_t pendingBookmarkSpine = UINT16_MAX;
  float pendingBookmarkProgress = -1.0f;
  uint16_t pendingBookmarkParagraphIndex = UINT16_MAX;
  uint16_t pendingClippingIndex = UINT16_MAX;

  // Set by background move task on failure; read and cleared by ActivityManager to show AlertActivity.
  // Title/body are written before the flag is set to ensure they are visible when flag is read.
  std::atomic<bool> hasPendingAlert{false};
  std::atomic<bool> pendingAlertGoHomeOnBack{false};
  char pendingAlertTitle[64] = {};
  char pendingAlertBody[256] = {};

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
