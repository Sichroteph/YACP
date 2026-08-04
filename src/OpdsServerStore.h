#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

enum class OpdsFilenameFormat : uint8_t {
  AUTHOR_TITLE = 0,
  TITLE_AUTHOR = 1,
};

const char* opdsFilenameFormatToJson(OpdsFilenameFormat format);
OpdsFilenameFormat opdsFilenameFormatFromJson(const char* value);

struct OpdsServer {
  std::string name;
  std::string url;
  std::string username;
  std::string password;  // Plaintext in memory; obfuscated with hardware key on disk
  OpdsFilenameFormat filenameFormat = OpdsFilenameFormat::AUTHOR_TITLE;
};

/**
 * Singleton class for storing OPDS server configurations on the SD card.
 * Passwords are XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON.
 */
class OpdsServerStore : public PersistableStore<OpdsServerStore> {
 private:
  enum class LoadState : uint8_t { NotLoaded, Loading, Loaded, Failed };

  std::vector<OpdsServer> servers;
  LoadState loadState = LoadState::NotLoaded;

  static constexpr size_t MAX_SERVERS = 8;

  OpdsServerStore() = default;
  bool migrateFromSettings();

  friend class PersistableStore<OpdsServerStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/opds.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
  bool loadFromFile();
  bool ensureLoaded();

  bool addServer(const OpdsServer& server);
  bool updateServer(size_t index, const OpdsServer& server);
  bool removeServer(size_t index);

  const std::vector<OpdsServer>& getServers() const {
    const_cast<OpdsServerStore*>(this)->ensureLoaded();
    return servers;
  }
  const OpdsServer* getServer(size_t index) const;
  size_t getCount() const {
    const_cast<OpdsServerStore*>(this)->ensureLoaded();
    return servers.size();
  }
  bool hasServers() const {
    const_cast<OpdsServerStore*>(this)->ensureLoaded();
    return !servers.empty();
  }
};

#define OPDS_STORE OpdsServerStore::getInstance()
