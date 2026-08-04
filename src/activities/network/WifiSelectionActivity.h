#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;
struct ThemeMetrics;

// Structure to hold WiFi network information
struct WifiNetworkInfo {
  std::string ssid;
  std::array<uint8_t, 6> bssid{};
  int32_t rssi = 0;
  uint8_t channel = 0;
  bool isEncrypted = false;
  bool hasSavedPassword = false;  // Whether we have saved credentials for this network
  bool hasAccessPointHint = false;
};

enum class WifiConnectionOrigin { AUTOMATIC, MANUAL };

// WiFi selection states
enum class WifiSelectionState {
  AUTO_SCANNING,      // Scanning before silently selecting a saved network
  AUTO_CONNECTING,    // Trying to connect to the last known network
  SCANNING,           // Scanning for networks
  NETWORK_LIST,       // Displaying available networks
  PASSWORD_ENTRY,     // Entering password for selected network
  CONNECTING,         // Attempting to connect
  CONNECTED,          // Successfully connected
  SAVE_PROMPT,        // Asking user if they want to save the password
  CONNECTION_FAILED,  // Connection failed
  FORGET_PROMPT       // Asking user if they want to forget the network
};

/**
 * WifiSelectionActivity is responsible for scanning WiFi APs and connecting to them.
 * It will:
 * - Enter scanning mode on entry
 * - List available WiFi networks
 * - Allow selection and launch KeyboardEntryActivity for password if needed
 * - Save the password if requested
 * - Call onComplete callback when connected or cancelled
 *
 * The onComplete callback receives true if connected successfully, false if cancelled.
 */
class WifiSelectionActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  WifiSelectionState state = WifiSelectionState::SCANNING;
  size_t selectedNetworkIndex = 0;
  std::vector<WifiNetworkInfo> networks;

  // Selected network for connection
  std::string selectedSSID;
  bool selectedRequiresPassword = false;

  // Connection result
  std::string connectedIP;
  std::string connectionError;

  // Password to potentially save (from keyboard or saved credentials)
  std::string enteredPassword;

  // Cached MAC address string for display
  std::string cachedMacAddress;

  // Whether network was connected using a saved password (skip save prompt)
  bool usedSavedPassword = false;

  // Whether to attempt auto-connect on entry
  const bool allowAutoConnect;

  bool tearDownWifiOnExit = false;

  // One bit per stored credential (WifiCredentialStore is capped at 8).
  uint8_t attemptedCredentialMask = 0;

  // Optional scan result used to skip the driver's second channel scan.
  std::array<uint8_t, 6> selectedBssid{};
  uint8_t selectedChannel = 0;
  bool hasSelectedAccessPointHint = false;

  // Save/forget prompt selection (0 = Yes, 1 = No)
  int savePromptSelection = 0;
  int forgetPromptSelection = 0;

  // Connection timeout
  static constexpr unsigned long CONNECTION_TIMEOUT_MS = 15000;
  static constexpr unsigned long AUTO_CONNECTION_TIMEOUT_MS = 7000;
  static constexpr unsigned long CONNECTION_STATUS_LOG_INTERVAL_MS = 2000;
  unsigned long connectionStartTime = 0;
  unsigned long lastConnectionStatusLogTime = 0;
  int lastLoggedWifiStatus = -1;

  void renderNetworkList(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderPasswordEntry(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderConnecting(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderConnected(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderSavePrompt(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderConnectionFailed(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderForgetPrompt(const Rect* screen, const ThemeMetrics* metrics) const;

  void startWifiScan(WifiConnectionOrigin origin = WifiConnectionOrigin::MANUAL);
  void processWifiScanResults();
  void selectNetwork(int index);
  void selectAccessPoint(const WifiNetworkInfo& network);
  void attemptConnection(WifiConnectionOrigin origin = WifiConnectionOrigin::MANUAL);
  void checkConnectionStatus();
  bool tryAutomaticCredential(size_t credentialIndex, const WifiNetworkInfo* network = nullptr);
  bool tryNextSavedNetworkFromScan();
  void handleAutomaticConnectionFailure();
  void showNetworkListFromAutomaticConnection();
  int findCredentialIndex(const std::string& ssid) const;
  std::string getSignalStrengthIndicator(int32_t rssi) const;

  void onComplete(bool connected);

 public:
  explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoConnect = true)
      : Activity("WifiSelection", renderer, mappedInput), allowAutoConnect(autoConnect) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
