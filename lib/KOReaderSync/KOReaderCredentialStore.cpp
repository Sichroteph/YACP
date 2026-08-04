#include "KOReaderCredentialStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <ObfuscationUtils.h>

namespace {
// Default sync server URL
constexpr char DEFAULT_SERVER_URL[] = "https://sync.koreader.rocks:443";
}  // namespace

void KOReaderCredentialStore::toJson(JsonDocument& doc) const {
  doc["username"] = username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(password);
  doc["serverUrl"] = serverUrl;
  doc["matchMethod"] = static_cast<uint8_t>(matchMethod);
}

bool KOReaderCredentialStore::loadFromFile() {
  loadState = LoadState::Loading;
  const bool hasStoreFile = Storage.exists(getFilePath());
  const bool loaded = PersistableStore<KOReaderCredentialStore>::loadFromFile();
  if (loaded || !hasStoreFile) {
    loadState = LoadState::Loaded;
    return true;
  }
  loadState = LoadState::Failed;
  return false;
}

bool KOReaderCredentialStore::ensureLoaded() {
  switch (loadState) {
    case LoadState::Loaded:
    case LoadState::Loading:
      return true;
    case LoadState::Failed:
      return false;
    case LoadState::NotLoaded:
      return loadFromFile();
  }
  return false;
}

bool KOReaderCredentialStore::fromJson(JsonVariantConst doc) {
  std::string user = doc["username"] | "";

  bool needsResave = false;
  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &status);
  if (status == obfuscation::DecodeStatus::LEGACY && !pass.empty()) {
    needsResave = true;
  }
  if (status == obfuscation::DecodeStatus::INVALID || status == obfuscation::DecodeStatus::EMPTY || pass.empty()) {
    pass = doc["password"] | "";
    if (!pass.empty()) needsResave = true;
  }
  if (status == obfuscation::DecodeStatus::INVALID && pass.empty()) {
    LOG_ERR("KRS", "Ignoring unreadable KOReader password");
  }

  setCredentials(user, pass);
  setServerUrl(doc["serverUrl"] | "");

  uint8_t method = doc["matchMethod"] | (uint8_t)0;
  if (method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)) {
    setMatchMethod(static_cast<DocumentMatchMethod>(method));
  } else {
    LOG_DBG("KRS", "Invalid matchMethod %u in JSON, resetting to FILENAME", method);
    setMatchMethod(DocumentMatchMethod::FILENAME);
  }

  if (needsResave) {
    LOG_DBG("KRS", "Resaved KOReader credentials to update format");
    saveToFile();
  }

  return true;
}

void KOReaderCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  if (!ensureLoaded()) return;
  username = user;
  password = pass;
  LOG_DBG("KRS", "Set credentials for user: %s", user.c_str());
}

std::string KOReaderCredentialStore::getMd5Password() const {
  const_cast<KOReaderCredentialStore*>(this)->ensureLoaded();
  if (password.empty()) {
    return "";
  }

  // Calculate MD5 hash of password using ESP32's MD5Builder
  MD5Builder md5;
  md5.begin();
  md5.add(password.c_str());
  md5.calculate();

  return md5.toString().c_str();
}

bool KOReaderCredentialStore::hasCredentials() const {
  const_cast<KOReaderCredentialStore*>(this)->ensureLoaded();
  return !username.empty() && !password.empty();
}

void KOReaderCredentialStore::clearCredentials() {
  if (!ensureLoaded()) return;
  username.clear();
  password.clear();
  saveToFile();
  LOG_DBG("KRS", "Cleared KOReader credentials");
}

void KOReaderCredentialStore::setServerUrl(const std::string& url) {
  if (!ensureLoaded()) return;
  serverUrl = url;
  LOG_DBG("KRS", "Set server URL: %s", url.empty() ? "(default)" : url.c_str());
}

std::string KOReaderCredentialStore::getBaseUrl() const {
  const_cast<KOReaderCredentialStore*>(this)->ensureLoaded();
  std::string url;
  if (serverUrl.empty()) {
    url = DEFAULT_SERVER_URL;
  } else if (serverUrl.find("://") == std::string::npos) {
    // Normalize URL: add http:// if no protocol specified (local servers typically don't have SSL)
    url = "http://" + serverUrl;
  } else {
    url = serverUrl;
  }

  // Strip trailing slashes to avoid double-slash in API paths
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }

  return url;
}

void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod method) {
  if (!ensureLoaded()) return;
  matchMethod = method;
  LOG_DBG("KRS", "Set match method: %s", method == DocumentMatchMethod::FILENAME ? "Filename" : "Binary");
}
