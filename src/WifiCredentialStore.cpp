#include "WifiCredentialStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>

#include <algorithm>
#include <utility>

namespace {
constexpr uint8_t WIFI_FILE_VERSION = 2;
constexpr char WIFI_FILE_BIN[] = "/.crosspoint/wifi.bin";
constexpr char WIFI_FILE_BAK[] = "/.crosspoint/wifi.bin.bak";
constexpr uint8_t LEGACY_OBFUSCATION_KEY[] = {0x43, 0x72, 0x6F, 0x73, 0x73, 0x50, 0x6F, 0x69, 0x6E, 0x74};
constexpr size_t LEGACY_KEY_LENGTH = sizeof(LEGACY_OBFUSCATION_KEY);

void legacyDeobfuscate(std::string& data) {
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= LEGACY_OBFUSCATION_KEY[i % LEGACY_KEY_LENGTH];
  }
}
}  // namespace

void WifiCredentialStore::toJson(JsonDocument& doc) const {
  doc["lastConnectedSsid"] = lastConnectedSsid;

  JsonArray arr = doc["credentials"].to<JsonArray>();
  for (const auto& cred : credentials) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = cred.ssid;
    obj["password_obf"] = obfuscation::obfuscateToBase64(cred.password);
  }
}

bool WifiCredentialStore::fromJson(JsonVariantConst doc) {
  lastConnectedSsid = doc["lastConnectedSsid"] | "";

  // Tolerate a missing/invalid 'credentials' key (treat as empty list); only
  // a JSON parse error is fatal. A null JsonArray iterates zero times.
  credentials.clear();
  JsonArrayConst arr = doc["credentials"].as<JsonArrayConst>();
  credentials.reserve(std::min(arr.size(), MAX_NETWORKS));
  bool needsResave = false;

  for (JsonObjectConst obj : arr) {
    if (credentials.size() >= MAX_NETWORKS) break;
    WifiCredential cred;
    cred.ssid = obj["ssid"] | "";
    if (cred.ssid.empty()) {
      LOG_ERR("WCS", "Skipping WiFi credential with empty SSID");
      continue;
    }

    obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
    cred.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &status);
    if (status == obfuscation::DecodeStatus::LEGACY && !cred.password.empty()) {
      needsResave = true;
    }
    if (status == obfuscation::DecodeStatus::INVALID || status == obfuscation::DecodeStatus::EMPTY ||
        cred.password.empty()) {
      cred.password = obj["password"] | "";
      if (!cred.password.empty()) needsResave = true;
    }
    if (status == obfuscation::DecodeStatus::INVALID && cred.password.empty()) {
      LOG_ERR("WCS", "Skipping WiFi credential with unreadable password: %s", cred.ssid.c_str());
      continue;
    }
    credentials.push_back(std::move(cred));
  }

  LOG_DBG("WCS", "Loaded %zu WiFi credentials from file", credentials.size());

  if (needsResave) {
    LOG_DBG("WCS", "Resaving JSON with obfuscated passwords");
    saveToFile();
  }

  return true;
}

bool WifiCredentialStore::loadFromFile() {
  const bool hasStoreFile = Storage.exists(getFilePath());
  if (PersistableStore<WifiCredentialStore>::loadFromFile()) {
    return true;
  }
  if (hasStoreFile) {
    return false;
  }

  if (Storage.exists(WIFI_FILE_BIN) && loadFromBinaryFile()) {
    if (saveToFile()) {
      Storage.rename(WIFI_FILE_BIN, WIFI_FILE_BAK);
      LOG_DBG("WCS", "Migrated wifi.bin to wifi.json");
      return true;
    }
    LOG_ERR("WCS", "Failed to save wifi during migration");
  }

  return false;
}

bool WifiCredentialStore::loadFromBinaryFile() {
  HalFile file;
  if (!Storage.openFileForRead("WCS", WIFI_FILE_BIN, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version > WIFI_FILE_VERSION) {
    LOG_DBG("WCS", "Unknown file version: %u", version);
    return false;
  }

  if (version >= 2) {
    serialization::readString(file, lastConnectedSsid);
  } else {
    lastConnectedSsid.clear();
  }

  uint8_t count;
  serialization::readPod(file, count);

  credentials.clear();
  credentials.reserve(std::min<size_t>(count, MAX_NETWORKS));
  for (uint8_t i = 0; i < count && i < MAX_NETWORKS; i++) {
    WifiCredential cred;
    serialization::readString(file, cred.ssid);
    serialization::readString(file, cred.password);
    legacyDeobfuscate(cred.password);
    credentials.push_back(std::move(cred));
  }

  return true;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  // Check if this SSID already exists and update it
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    cred->password = password;
    LOG_DBG("WCS", "Updated credentials for: %s", ssid.c_str());
    return saveToFile();
  }

  // Check if we've reached the limit
  if (credentials.size() >= MAX_NETWORKS) {
    LOG_DBG("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
    return false;
  }

  // Add new credential
  credentials.push_back({ssid, password});
  LOG_DBG("WCS", "Added credentials for: %s", ssid.c_str());
  return saveToFile();
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    credentials.erase(cred);
    LOG_DBG("WCS", "Removed credentials for: %s", ssid.c_str());
    if (ssid == lastConnectedSsid) {
      clearLastConnectedSsid();
    }
    return saveToFile();
  }
  return false;  // Not found
}

const WifiCredential* WifiCredentialStore::findCredential(const std::string& ssid) const {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });

  if (cred != credentials.end()) {
    return &*cred;
  }

  return nullptr;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const { return findCredential(ssid) != nullptr; }

void WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) {
  if (lastConnectedSsid != ssid) {
    lastConnectedSsid = ssid;
    saveToFile();
  }
}

const std::string& WifiCredentialStore::getLastConnectedSsid() const { return lastConnectedSsid; }

void WifiCredentialStore::clearLastConnectedSsid() {
  if (!lastConnectedSsid.empty()) {
    lastConnectedSsid.clear();
    saveToFile();
  }
}

void WifiCredentialStore::clearAll() {
  credentials.clear();
  lastConnectedSsid.clear();
  saveToFile();
  LOG_DBG("WCS", "Cleared all WiFi credentials");
}
