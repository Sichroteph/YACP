#include "LegacyMigration.h"

#include <CredentialIntegrity.h>
#include <ObfuscationUtils.h>
#include <esp_mac.h>

#include <cmath>
#include <functional>

#include "BookmarkUtil.h"
#include "LegacyAnchor.h"
#include "LegacySettings.h"
#include "LegacyTextPosition.h"

namespace legacy {
namespace {
constexpr uint8_t MARK[] = {'Y', 'A', 'C', 'P', 'M', 'I', 'G', 1};
constexpr uint8_t RECEIPT_MARK[] = {'Y', 'A', 'C', 'P', 'R', 'C', 'P', 2};
constexpr uint8_t TEXT_RECEIPT_MARK[] = {'Y', 'T', 'X', 1};
constexpr char CHECKED[] = "/.crosspoint/data-checked-v3";
constexpr char BOOK_MOVE[] = "/book-move.bin";
bool failed = false;
bool backupFailed = false;

uint32_t gccHash(const std::string& path) {
  const auto* data = reinterpret_cast<const uint8_t*>(path.data());
  constexpr uint32_t m = 0x5bd1e995;
  uint32_t h = 0xc70f6907u ^ path.size();
  size_t i = 0;
  for (; i + 4 <= path.size(); i += 4) {
    uint32_t k = u32(data + i) * m;
    k = (k ^ (k >> 24)) * m;
    h = h * m ^ k;
  }
  if (i < path.size()) {
    uint32_t tail = 0;
    for (unsigned shift = 0; i < path.size(); ++i, shift += 8) tail |= uint32_t(data[i]) << shift;
    h = (h ^ tail) * m;
  }
  h = (h ^ (h >> 13)) * m;
  return h ^ (h >> 15);
}
bool marker(const std::string& path) {
  auto f = Storage.open(path.c_str());
  uint8_t data[sizeof(MARK)];
  return f && f.size() == sizeof(MARK) && f.read(data, sizeof(data)) == sizeof(data) &&
         !memcmp(data, MARK, sizeof(data)) && f.close();
}

struct Migration {
  Io io;
  char sourceVersion[64]{};
  char name[256]{};
  char line[1024]{};
  int sdRange = 0;
  unsigned books = 0, positions = 0, reviews = 0;
  std::unique_ptr<Anchor> anchor;
  // Boot/maintenance only. Reused for each book, never retained by a reader.
  uint8_t sourceReceipt[160]{};
  bool inspecting = false, reentry = false, repairNeeded = false, textOnly = false;
  const char* liveSource = "/.crosspoint";
  static constexpr const char* REPAIR = "/.yacp-reentry-v2/pending";
  static constexpr const char* REPAIR_SOURCE = "/.yacp-reentry-v2/pending/original";
  static constexpr const char* REPAIR_TARGET = "/.yacp-reentry-v2/pending/staging";
  static constexpr const char* SOURCE_BACKUP = "/.yacp-source-backup/original";
  static constexpr const char* SOURCE_BACKUP_PENDING = "/.yacp-source-backup/pending";
  static constexpr const char* UPGRADE_BACKUP = "/.yacp-source-backup/pre-crosspoint-2.0";
  static constexpr const char* UPGRADE_BACKUP_PENDING = "/.yacp-source-backup/pre-crosspoint-2.0.pending";
  static constexpr const char* NATIVE_SETTINGS = "/.crosspoint/settings.json";
  static constexpr const char* CROSSINK_SETTINGS = "/.crosspoint/crossink-settings.json";

  int readSmall(const std::string& path, uint8_t* bytes, size_t capacity, bool prefix = false) {
    auto file = Storage.open(path.c_str());
    if (!file) return -1;
    const auto size = prefix ? capacity : file.size();
    const bool valid = !file.isDirectory() && size <= capacity && file.size() >= size;
    const bool read = valid && file.read(bytes, size) == int(size);
    const bool closed = file.close();
    if (!read || !closed) {
      LOG_ERR("MIG", "Cannot read bounded data record: %s", path.c_str());
      return -1;
    }
    return int(size);
  }
  bool sameBytes(const std::string& path, const uint8_t* bytes, size_t size) {
    return readSmall(path, io.a, size) == int(size) && !memcmp(io.a, bytes, size);
  }
  bool removeStaged(const std::string& path) {
    // Only the disposable, inactive copy is replaced. The full source tree
    // remains intact in REPAIR_SOURCE through every interruption.
    if (path.rfind(std::string(REPAIR_TARGET) + "/", 0) != 0) return false;
    return !Storage.exists(path.c_str()) || Storage.remove(path.c_str());
  }
  std::string nextSnapshot(const char* root) {
    for (unsigned index = 1; index; ++index) {
      const auto path = std::string(root) + "/" + std::to_string(index);
      if (!Storage.exists(path.c_str())) return path;
    }
    return {};
  }
  bool repairWifi() {
    const std::string path = std::string(liveSource) + "/wifi.json";
    if (!Storage.exists(path.c_str())) return true;
    JsonMemory memory;
    JsonDocument doc(&memory);
    if (!io.readJson(path, doc) || !doc["credentials"].is<JsonArray>() || doc["credentials"].size() > 8) return false;
    bool changed = false;
    for (JsonVariant entry : doc["credentials"].as<JsonArray>()) {
      if (!entry.is<JsonObject>()) return false;
      JsonObject row = entry.as<JsonObject>();
      const char* ssid = row["ssid"] | "";
      if (!*ssid || strlen(ssid) > 32 || row["ssid"].as<JsonString>().size() != strlen(ssid)) return false;
      bool ok = false, tooLong = false;
      const char* encoded = row["password_obf"] | "";
      auto value = *encoded ? obfuscation::deobfuscateFromBase64(encoded, 72, &ok, &tooLong) : std::string();
      if (!*encoded) ok = true;
      if (!ok || tooLong) return false;
      const bool wrapped = value.size() >= 4 && !memcmp(value.data(), "CPV1", 4);
      const bool length = !row["password_len"].isNull(), checksum = !row["password_crc32"].isNull();
      if (length != checksum ||
          (length && (!row["password_len"].is<unsigned>() || !row["password_crc32"].is<uint32_t>())))
        return false;
      if (length && (row["password_len"].as<size_t>() != value.size() ||
                     row["password_crc32"].as<uint32_t>() != credential_integrity::crc32(value)))
        return false;
      if (!wrapped) {
        if (value.size() > 64 || value.find('\0') != std::string::npos) return false;
        continue;  // Retain valid manually entered native credentials byte for byte.
      }
      // Reuse the first-migration validator and encoder, including the
      // factory-bound CPV1 checksum. Only this cold path decodes twice.
      value.clear();
      if (!credential(row, true)) return false;
      changed = true;
    }
    if (!changed) return true;
    repairNeeded = true;
    if (inspecting) return true;
    const std::string target = std::string(REPAIR_TARGET) + "/wifi.json";
    if (!removeStaged(target) || !io.writeJson(target, doc)) return false;
    LOG_INF("MIG", "Converted validated YACP WiFi credentials");
    checkpoint("reentry-wifi");
    return true;
  }
  struct TextRecord {
    // Marker, progress length, 6 progress bytes, reserved byte, source file
    // size, checked raw byte offset, and checksum. All fields are little-endian.
    uint8_t receipt[24]{};
    uint32_t size() const { return u32(receipt + 12); }
    uint32_t offset() const { return u32(receipt + 16); }
  };
  bool readTextOffset(const std::string& source, uint32_t fileSize, uint16_t page, uint32_t& offset) {
    auto index = Storage.open((source + "/index.bin").c_str());
    if (!index) return false;
    uint8_t header[30];
    const bool validHeader = readAt(index, 0, header, sizeof(header)) && !memcmp(header, "ITXT\3", 5) &&
                             u32(header + 5) == fileSize;
    const uint32_t count = validHeader ? u32(header + 26) : 0;
    bool valid = page < count && uint64_t(count) * 4 + sizeof(header) == index.size();
    uint32_t previous = 0;
    for (uint32_t start = 0; valid && start < count;) {
      const uint32_t entries = std::min<uint32_t>(count - start, sizeof(io.a) / 4);
      if (index.read(io.a, entries * 4) != static_cast<int>(entries * 4)) {
        valid = false;
        break;
      }
      for (uint32_t i = 0; i < entries; ++i) {
        const uint32_t value = u32(io.a + i * 4);
        if ((start + i == 0 && value != 0) || value < previous || value > fileSize) {
          valid = false;
          break;
        }
        if (start + i == page) offset = value;
        previous = value;
      }
      start += entries;
      delay(1);
    }
    return index.close() && valid;
  }
  bool readTextRecord(const std::string& path, const std::string& source, TextRecord& record) {
    const std::string identity = source + "/migration-source-path";
    if (Storage.exists(identity.c_str()) &&
        (readSmall(identity, io.b, sizeof(io.b)) != static_cast<int>(path.size()) ||
         memcmp(io.b, path.data(), path.size())))
      return false;
    const int length = readSmall(source + "/progress.bin", record.receipt + 5, 6);
    if (length != 4 && length != 6) return false;
    auto book = Storage.open(path.c_str());
    if (!book) return false;
    const auto size = book.size();
    if (!book.close() || size >= UINT32_MAX) return false;
    uint32_t rawOffset = 0;
    if (length == 6) {
      rawOffset = u32(record.receipt + 7);
    } else if (!readTextOffset(source, size, u16(record.receipt + 5), rawOffset)) {
      return false;
    }
    if (rawOffset > size) return false;
    memcpy(record.receipt, TEXT_RECEIPT_MARK, sizeof(TEXT_RECEIPT_MARK));
    record.receipt[4] = length;
    put32(record.receipt + 12, size);
    put32(record.receipt + 16, rawOffset);
    put32(record.receipt + 20, hash32(record.receipt, 20));
    return true;
  }
  std::optional<uint32_t> textAnchor(const std::string& path, const TextRecord& record) {
    auto book = Storage.open(path.c_str());
    if (!book) return std::nullopt;
    if (book.size() != record.size()) {
      book.close();
      return std::nullopt;
    }
    size_t pos = 0, available = 0;
    unsigned blocks = 0;
    const auto anchor = txt::visibleOffset(record.offset(), record.size(), [&]() -> int {
      if (pos == available) {
        const int read = book.read(io.a, sizeof(io.a));
        if (read <= 0) return -1;
        available = static_cast<size_t>(read);
        pos = 0;
        if (++blocks % 16 == 0) delay(1);
      }
      return io.a[pos++];
    });
    return book.close() ? anchor : std::nullopt;
  }
  bool copyTextStats(const std::string& source, const std::string& target, const std::string& native = {},
                     const std::string& preferred = {}) {
    for (const char* stat :
         {"stats.bin", "stats_v2.bin", "stats_v3.bin", "stats_v4.bin", "stats_v5.bin", "stats_v6.bin"}) {
      for (const char* suffix : {"", ".bak"}) {
        const std::string file = std::string("/") + stat + suffix;
        if ((!native.empty() && Storage.exists((native + file).c_str())) ||
            (!preferred.empty() && Storage.exists((preferred + file).c_str()))) continue;
        const std::string from = source + file;
        if (Storage.exists(from.c_str()) && !io.copy(from, target + file)) return false;
      }
    }
    return true;
  }
  bool retireTextCache(const std::string& key) {
    const std::string staged = std::string(REPAIR_TARGET) + "/" + key;
    const std::string retired = std::string(REPAIR) + "/retired-txt/" + key;
    // Keep the original and the retired copy in the same repair history.
    // A later rename, deletion or reuse of the old book path must never
    // activate this retired position again.
    if (Storage.exists(retired.c_str())) return !Storage.exists(staged.c_str());
    if (io.parent(retired) && Storage.rename(staged.c_str(), retired.c_str())) return true;
    LOG_ERR("MIG", "Cannot retire old plain text cache: %s", key.c_str());
    return false;
  }
  bool repairText(const std::string& path) {
    // A pending move can put the old cache at the destination while the
    // book itself still has its original path. Scan again after recovery.
    if (Storage.exists((std::string(liveSource) + BOOK_MOVE).c_str())) return true;
    const auto pathKey = std::to_string(std::hash<std::string>{}(path));
    const std::string keys[] = {"txt_" + pathKey, "txt_" + std::to_string(pathHash(path))};
    TextRecord records[2];
    bool seen[2]{};
    bool changed[2]{};
    bool acknowledged = true;
    std::optional<uint32_t> anchor;
    for (unsigned i = 0; i < 2; ++i) {
      if (i && keys[i] == keys[0]) break;
      const std::string source = std::string(liveSource) + "/" + keys[i];
      if (!Storage.exists((source + "/progress.bin").c_str())) continue;
      seen[i] = true;
      if (!readTextRecord(path, source, records[i])) {
        LOG_ERR("MIG", "Plain text position requires review: %s", path.c_str());
        return false;
      }
      const std::string receipt = std::string(liveSource) + "/txt-receipts/" + keys[i];
      const bool known = Storage.exists(receipt.c_str());
      if (known) {
        if (readSmall(receipt, io.b, sizeof(records[i].receipt)) != sizeof(records[i].receipt) ||
            memcmp(io.b, TEXT_RECEIPT_MARK, sizeof(TEXT_RECEIPT_MARK)) || u32(io.b + 20) != hash32(io.b, 20)) {
          LOG_ERR("MIG", "Corrupt plain text position receipt: %s", path.c_str());
          return false;
        }
        if (!memcmp(io.b, records[i].receipt, sizeof(records[i].receipt))) continue;
      }
      acknowledged = acknowledged && known;
      changed[i] = true;
      const auto candidate = textAnchor(path, records[i]);
      if (!candidate || (anchor && anchor != candidate)) {
        LOG_ERR("MIG", "Unresolved plain text anchor: %s", path.c_str());
        return false;
      }
      anchor = candidate;
    }
    if (!seen[0] && !seen[1]) return true;

    const std::string native = std::string(liveSource) + "/epub_" + pathKey;
    const std::string nativeProgress = native + "/progress.bin";
    bool sameAnchor = false;
    if (anchor && Storage.exists(nativeProgress.c_str())) {
      uint8_t previous[10]{};
      const int size = readSmall(nativeProgress, previous, sizeof(previous));
      if (size != 4 && size != 6 && size != 10) return false;
      const bool meaningful = u16(previous) || u16(previous + 2) || u32(previous + 6);
      sameAnchor = size == 10 && !u16(previous) && u32(previous + 6) == *anchor;
      if (meaningful && !sameAnchor && !acknowledged) {
        LOG_ERR("MIG", "Conflicting native and old plain text positions: %s", path.c_str());
        return false;
      }
    }
    repairNeeded = true;
    if (inspecting) return true;

    const std::string target = std::string(REPAIR_TARGET) + "/epub_" + pathKey;
    uint8_t result[10]{};
    if (anchor && !sameAnchor) {
      put32(result + 6, *anchor);
      if (!removeStaged(target + "/progress.bin") || !io.append(target + "/progress.bin", result, sizeof(result)))
        return false;
    }
    const std::string firstSource = changed[0] ? std::string(liveSource) + "/" + keys[0] : std::string();
    for (unsigned i = 0; i < 2; ++i) {
      if (changed[i]) {
        const std::string receipt = std::string(REPAIR_TARGET) + "/txt-receipts/" + keys[i];
        if (!copyTextStats(std::string(liveSource) + "/" + keys[i], target, native, i ? firstSource : std::string()) ||
            !removeStaged(receipt) ||
            !io.append(receipt, records[i].receipt, sizeof(records[i].receipt))) return false;
      }
      if (seen[i] && !retireTextCache(keys[i])) return false;
    }
    checkpoint("reentry-text");
    return true;
  }
  bool repairBook(const std::string& path, const char* kind) {
    if (!strcmp(kind, "txt")) return repairText(path);
    if (textOnly) return true;
    const std::string key = std::string(kind) + "_" + std::to_string(pathHash(path));
    const std::string native = std::string(kind) + "_" + std::to_string(std::hash<std::string>{}(path));
    const std::string source = std::string(liveSource) + "/" + key;
    if (key == native || !Storage.exists((source + "/progress.bin").c_str())) return true;
    memset(sourceReceipt, 0, sizeof(sourceReceipt));
    memcpy(sourceReceipt, RECEIPT_MARK, sizeof(RECEIPT_MARK));
    const int size = readSmall(source + "/progress.bin", sourceReceipt + 9, 10);
    if (size != 4 && size != 6 && size != 10) return false;
    sourceReceipt[8] = size;
    unsigned mode = 0;
    if (Storage.exists((source + "/reader_settings.bin").c_str())) {
      const int profileSize = readSmall(source + "/reader_settings.bin", sourceReceipt + 20, 86);
      if (profileSize != 3 && profileSize != 86) return false;
      sourceReceipt[19] = profileSize;
      if ((profileSize == 3 && sourceReceipt[20] != 1) ||
          (profileSize == 86 && (sourceReceipt[20] != 2 || (sourceReceipt[21] & ~7))))
        return false;
      if (sourceReceipt[20] == 2 && (sourceReceipt[21] & 4)) mode = sourceReceipt[24];
      if (mode > 2) return false;
    }
    const auto chapter = section(source, u16(sourceReceipt + 9), mode);
    if (Storage.exists(chapter.c_str())) {
      if (readSmall(chapter, sourceReceipt + 107, 44, true) != 44) return false;
      sourceReceipt[106] = 44;
    }
    put32(sourceReceipt + 151, hash32(sourceReceipt, 151));
    const std::string receipt = "/legacy-receipts/" + key;
    const bool hasReceipt = Storage.exists((std::string(liveSource) + receipt).c_str());
    const bool known =
        hasReceipt &&
        readSmall(std::string(liveSource) + receipt, io.b, sizeof(sourceReceipt)) == sizeof(sourceReceipt) &&
        !memcmp(io.b, RECEIPT_MARK, sizeof(RECEIPT_MARK)) && u32(io.b + 151) == hash32(io.b, 151);
    if (hasReceipt && !known) {
      LOG_ERR("MIG", "Corrupt source receipt; refusing progress replay");
      return false;
    }
    if (sameBytes(std::string(liveSource) + receipt, sourceReceipt, sizeof(sourceReceipt))) return true;
    repairNeeded = true;
    if (inspecting) return true;
    if (strcmp(kind, "epub")) {
      LOG_ERR("MIG", "Mixed non-EPUB position requires review: %s", path.c_str());
      return false;
    }
    auto epub = makeUniqueNoThrow<Epub>(path, REPAIR_TARGET);
    if (!epub || !epub->load(true, true)) return false;
    const std::string target = std::string(REPAIR_TARGET) + "/" + native;
    const std::string scratch = std::string(REPAIR) + "/candidates/" + native;
    if (!progress(*epub, source, scratch, mode)) return false;
    uint8_t candidate[10]{};
    if (readSmall(scratch + "/progress.bin", candidate, sizeof(candidate)) != sizeof(candidate)) return false;
    uint8_t previous[10]{};
    bool meaningful = false;
    if (Storage.exists((std::string(liveSource) + "/" + native + "/progress.bin").c_str())) {
      const int count = readSmall(std::string(liveSource) + "/" + native + "/progress.bin", previous, sizeof(previous));
      if (count != 4 && count != 6 && count != 10) return false;
      meaningful = u16(previous) || u16(previous + 2) || u32(previous + 6);
      // A previous receipt proves the legacy writer changed after our last
      // conversion. Without one, two different useful positions are ambiguous.
      if (meaningful && !known &&
          (count != 10 || u16(previous) != u16(candidate) || u32(previous + 6) != u32(candidate + 6))) {
        LOG_ERR("MIG", "Conflicting native/legacy positions; originals retained: %s", path.c_str());
        return false;
      }
    }
    if (!removeStaged(target + "/progress.bin") || !io.append(target + "/progress.bin", candidate, sizeof(candidate)))
      return false;
    for (const char* stat :
         {"stats.bin", "stats_v2.bin", "stats_v3.bin", "stats_v4.bin", "stats_v5.bin", "stats_v6.bin"}) {
      for (const char* suffix : {"", ".bak"}) {
        const std::string from = source + "/" + stat + suffix;
        const std::string to = target + "/" + stat + suffix;
        if (!Storage.exists(from.c_str())) continue;
        // Existing meaningful native statistics are never replaced silently.
        if (meaningful && Storage.exists(to.c_str())) continue;
        if (!removeStaged(to) || !io.copy(from, to)) return false;
      }
    }
    if (!removeStaged(std::string(REPAIR_TARGET) + receipt) ||
        !io.append(std::string(REPAIR_TARGET) + receipt, sourceReceipt, sizeof(sourceReceipt)))
      return false;
    checkpoint("reentry-book");
    return true;
  }
  bool inspectCurrent() {
    inspecting = reentry = true;
    repairNeeded = false;
    liveSource = "/.crosspoint";
    const bool ok = (textOnly || repairWifi()) && scan("/");
    inspecting = reentry = false;
    if (!ok) LOG_ERR("MIG", "Current data has an invalid or unresolved legacy format");
    return ok;
  }
  bool recoverCurrent() {
    if (!Storage.exists(REPAIR)) {
      if (!inspectCurrent()) return false;
      if (!repairNeeded) return true;
      if (!Storage.ensureDirectoryExists(REPAIR) || !Storage.rename("/.crosspoint", REPAIR_SOURCE)) return false;
      checkpoint("reentry-original");
    }
    if (!Storage.exists(REPAIR_SOURCE)) {
      // Interruption between mkdir and rename: no data was activated.
      if (!Storage.exists("/.crosspoint") || !Storage.rename("/.crosspoint", REPAIR_SOURCE)) return false;
    }
    if (!Storage.exists("/.crosspoint")) {
      const std::string copied = std::string(REPAIR) + "/copied";
      if (!marker(copied)) {
        if (!copyTree(REPAIR_SOURCE, REPAIR_TARGET) || !io.append(copied, MARK, sizeof(MARK))) return false;
      }
      checkpoint("reentry-copied");
      liveSource = REPAIR_SOURCE;
      reentry = true;
      const bool ready = (textOnly || repairWifi()) && scan("/");
      reentry = false;
      if (!ready || !io.append(std::string(REPAIR) + "/prepared", MARK, sizeof(MARK))) return false;
      checkpoint("reentry-prepared");
      if (!Storage.rename(REPAIR_TARGET, "/.crosspoint")) return false;
      checkpoint("reentry-activated");
    } else if (Storage.exists(REPAIR_TARGET) || !marker(std::string(REPAIR) + "/prepared"))
      return false;
    const auto history = nextSnapshot("/.yacp-reentry-v2/history");
    if (history.empty() || !io.parent(history) || !Storage.rename(REPAIR, history.c_str())) return false;
    LOG_INF("MIG", "Mixed YACP store recovered; original retained at %s", history.c_str());
    return true;
  }
  bool recoverNativeTextIfPresent() {
    // On stock cards without an old TXT cache, avoid scanning the entire
    // library on cold startup. A later boot of the old reader can recreate it.
    auto root = Storage.open("/.crosspoint");
    if (!root || !root.isDirectory()) return false;
    bool found = false;
    while (auto entry = root.openNextFile()) {
      if (!entry.getName(name, sizeof(name))) {
        entry.close();
        root.close();
        return false;
      }
      found = entry.isDirectory() && !strncmp(name, "txt_", 4);
      if (!entry.close()) {
        root.close();
        return false;
      }
      if (found) break;
    }
    return root.close() && (!found || recoverCurrent());
  }
  bool review(const std::string& source, const char* reason) {
    ++reviews;
    LOG_ERR("MIG", "%s: %s (original retained)", source.c_str(), reason);
    const std::string path = std::string(STAGING) + "/migration-review/" + std::to_string(pathHash(source)) + ".json";
    JsonMemory docMemory;
    JsonDocument doc(&docMemory);
    doc["source"] = source.c_str();
    doc["reason"] = reason;
    return io.writeJson(path, doc);
  }
  bool copyTree(const std::string& source, const std::string& target, unsigned depth = 0) {
    if (depth > 12 || source.size() > 480 || target.size() > 480) return false;
    auto dir = Storage.open(source.c_str());
    if (!dir || !dir.isDirectory() || !Storage.ensureDirectoryExists(target.c_str())) return false;
    uint32_t position = 0;
    while (true) {
      if (!dir.seekSet(position)) return false;
      auto item = dir.openNextFile();
      if (!item) {
#ifndef SIMULATOR
        if (dir.hasError()) return false;
#endif
        break;
      }
      const bool directory = item.isDirectory();
      if (!item.getName(name, sizeof(name))) return false;
      const std::string leaf = name;
      position = dir.position();
      if (!item.close() || !dir.close()) return false;
      if (leaf == "." || leaf == ".." || leaf.find('/') != std::string::npos) return false;
      if (!(directory ? copyTree(source + "/" + leaf, target + "/" + leaf, depth + 1)
                      : io.copy(source + "/" + leaf, target + "/" + leaf)))
        return false;
      dir = Storage.open(source.c_str());
      if (!dir) return false;
    }
    return dir.close();
  }
  bool preserveSourceStore(const char* archivePath, const char* pendingPath) {
    if (Storage.exists(archivePath)) {
      auto archive = Storage.open(archivePath);
      return archive && archive.isDirectory() && archive.close() && !Storage.exists(pendingPath);
    }
    if (!copyTree("/.crosspoint", pendingPath)) {
      LOG_ERR("MIG", "Cannot copy source data; check the SD card and free space");
      return false;
    }
    checkpoint("source-backup-copied");
    if (!Storage.rename(pendingPath, archivePath)) {
      LOG_ERR("MIG", "Cannot finalize source data backup");
      return false;
    }
    LOG_INF("MIG", "Pre-update data preserved at %s", archivePath);
    checkpoint("source-backup-published");
    return true;
  }
  bool requireUpgradeBackup() {
    if (!Storage.exists(UPGRADE_BACKUP_PENDING) && (marker(CHECKED) || !Storage.exists(NATIVE_SETTINGS))) return true;
    backupFailed = !preserveSourceStore(UPGRADE_BACKUP, UPGRADE_BACKUP_PENDING);
    return !backupFailed;
  }
  bool isNativeYacp() {
    JsonMemory memory;
    JsonDocument doc(&memory);
    return io.readJson(NATIVE_SETTINGS, doc) &&
           ((doc["yacpReaderFontInitialized"] | false) || !doc["clockDateHasBeenSynced"].isNull());
  }
  bool requireSourceBackup() {
    if (marker(CHECKED) && !Storage.exists(SOURCE_BACKUP_PENDING) && !Storage.exists(UPGRADE_BACKUP_PENDING))
      return true;
    if (!Storage.exists(SOURCE_BACKUP_PENDING) && (Storage.exists(UPGRADE_BACKUP_PENDING) || isNativeYacp()))
      return requireUpgradeBackup();
    backupFailed = !preserveSourceStore(SOURCE_BACKUP, SOURCE_BACKUP_PENDING);
    return !backupFailed;
  }
  bool importCrossInkSettings() {
    if (Storage.exists(NATIVE_SETTINGS)) return true;

    JsonMemory sourceMemory;
    JsonMemory targetMemory;
    JsonDocument source(&sourceMemory);
    JsonDocument target(&targetMemory);
    if (!io.readJson(CROSSINK_SETTINGS, source) || !source.is<JsonObject>()) {
      LOG_ERR("MIG", "Cannot read CrossInk settings; original retained");
      return false;
    }
    if (!convertCrossInkSettings(source, target)) return false;

    constexpr const char* pending = "/.yacp-source-backup/crossink-settings.pending";
    if (!io.writeJson(pending, target) || !Storage.rename(pending, NATIVE_SETTINGS)) {
      LOG_ERR("MIG", "Cannot activate imported CrossInk settings; original retained");
      return false;
    }
    LOG_INF("MIG", "Imported compatible CrossInk settings");
    return true;
  }
  bool settings() {
    JsonMemory docMemory;
    JsonDocument doc(&docMemory);
    if (!io.readJson(std::string(ORIGINAL) + "/crossink-settings.json", doc)) return false;
    const char* version = doc["buttonLayoutPromptVersion"] | "";
    if (strlen(version) >= sizeof(sourceVersion)) return false;
    strcpy(sourceVersion, version);
    sdRange = doc["sdFontSizeRange"] | 0;
    if (!convertSettings(doc, sourceVersion, sdRange)) return false;
    // Frozen YACP kept these defaults when the fields were omitted.
    if (doc["trackReadingStats"].isNull()) doc["trackReadingStats"] = 1;
    if (doc["statusBarProgressBarPosition"].isNull()) doc["statusBarProgressBarPosition"] = 0;
    doc["yacpMigrationSchema"] = 1;
    if (!io.writeJson(std::string(STAGING) + "/settings.json", doc)) return false;
    checkpoint("settings");
    return true;
  }
  bool sharedState() {
    JsonMemory docMemory;
    JsonDocument doc(&docMemory);
    const std::string source = std::string(ORIGINAL) + "/state.json";
    if (Storage.exists(source.c_str()) && !io.readJson(source, doc)) return false;
    doc["showBootScreen"] = true;
    doc["lastSleepFromReader"] = false;
    doc["lastSleepRenderedQuickResume"] = false;
    doc["sleepFrameInverted"] = false;
    doc["readerActivityLoadCount"] = 0;
    for (const char* key : {"recentSleepImages", "recentOverlaySleepImages"}) {
      auto ring = doc[key].to<JsonArray>();
      for (unsigned i = 0; i < 16; ++i) ring.add(0);
    }
    for (const char* key : {"recentSleepPos", "recentSleepFill", "recentOverlaySleepPos", "recentOverlaySleepFill"})
      doc[key] = 0;
    return io.writeJson(std::string(STAGING) + "/state.json", doc);
  }
  bool credential(JsonObject row, bool wifi) {
    const char* encoded = row["password_obf"] | "";
    std::string password;
    if (*encoded) {
      bool ok = false, tooLong = false;
      password = obfuscation::deobfuscateFromBase64(encoded, 4104, &ok, &tooLong);
      if (!ok || tooLong || password.size() < 8 || memcmp(password.data(), "CPV1", 4)) return false;
      uint8_t mac[6];
      if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;
      const uint32_t expected = u32(reinterpret_cast<const uint8_t*>(password.data() + 4));
      const uint32_t actual =
          hash32(reinterpret_cast<const uint8_t*>(password.data() + 8), password.size() - 8, hash32(mac, sizeof(mac)));
      if (expected != actual) return false;
      password.erase(0, 8);
      if (!row["password"].isNull() && password != (row["password"] | "")) return false;
    } else
      password = row["password"] | "";
    if (password.find('\0') != std::string::npos || password.size() > (wifi ? 64u : 4096u)) return false;
    row["password_obf"] = obfuscation::obfuscateToBase64(password);
    row.remove("password");
    if (wifi) {
      row["password_len"] = password.size();
      row["password_crc32"] = credential_integrity::crc32(password);
    }
    return true;
  }
  bool credentials(const char* store) {
    const std::string source = std::string(ORIGINAL) + "/" + store + ".json";
    JsonMemory docMemory;
    JsonDocument doc(&docMemory);
    const bool wifi = strcmp(store, "wifi") == 0, opds = strcmp(store, "opds") == 0;
    if (!Storage.exists(source.c_str())) {
      if (!opds) return true;
      JsonMemory oldMemory;
      JsonDocument old(&oldMemory);
      if (!io.readJson(std::string(ORIGINAL) + "/crossink-settings.json", old)) return false;
      const char* url = old["opdsServerUrl"] | "";
      if (!*url) return true;
      auto row = doc["servers"].to<JsonArray>().add<JsonObject>();
      row["name"] = "OPDS";
      row["url"] = url;
      row["username"] = old["opdsUsername"] | "";
      if (!old["opdsPassword_obf"].isNull())
        row["password_obf"] = old["opdsPassword_obf"];
      else
        row["password"] = old["opdsPassword"] | "";
    } else if (!io.readJson(source, doc)) {
      review(source, "invalid_credential_store");
      return false;
    }
    if (wifi || opds) {
      const char* key = wifi ? "credentials" : "servers";
      if (!doc[key].is<JsonArray>() || doc[key].size() > 8) {
        review(source, "unsupported_credential_store");
        return false;
      }
      for (JsonObject row : doc[key].as<JsonArray>()) {
        if (!credential(row, wifi)) {
          review(source, "credential_key_or_format_requires_review");
          return false;
        }
        if (opds) {
          const char* format = row["filenameFormat"] | "author_title";
          if (strcmp(format, "author_title") && strcmp(format, "title_author"))
            return review(source, "unknown_filename_format");
          row["filenameFormat"] = strcmp(format, "title_author") == 0 ? 1 : 0;
        }
      }
    } else {
      if (!credential(doc.as<JsonObject>(), false)) {
        review(source, "credential_key_or_format_requires_review");
        return false;
      }
      if (!strlen(doc["serverUrl"] | "")) doc["serverUrl"] = "https://sync.koreader.rocks:443";
      doc["cfgVersion"] = 2;
      doc["sendMetadata"] = false;
    }
    return io.writeJson(std::string(STAGING) + "/" + store + ".json", doc);
  }
  bool nativeWifiCredentials() { return recoverCurrent(); }
  bool globals() {
    if (!settings() || !sharedState()) return false;
    for (const char* file : {"global_stats.bin", "global_stats.bin.bak", "daily_reading.bin", "daily_reading.bin.bak",
                             "finished_books.bin", "finished_books.bin.bak"}) {
      const std::string source = std::string(ORIGINAL) + "/" + file;
      if (Storage.exists(source.c_str()) && !io.copy(source, std::string(STAGING) + "/" + file)) return false;
    }
    for (const char* folder : {"sleep_images", "synced_stats"}) {
      const std::string source = std::string(ORIGINAL) + "/" + folder;
      if (Storage.exists(source.c_str()) && !copyTree(source, std::string(STAGING) + "/" + folder)) return false;
    }
    const std::string recent = std::string(ORIGINAL) + "/recent.json";
    if (Storage.exists(recent.c_str())) {
      JsonMemory docMemory;
      JsonDocument doc(&docMemory);
      if (!io.readJson(recent, doc)) return false;
      if (!doc["books"].is<JsonArray>() || doc["books"].size() > 18) return false;
      for (JsonObject book : doc["books"].as<JsonArray>()) book["coverBmpPath"] = "";
      if (!io.writeJson(std::string(STAGING) + "/recent.json", doc)) return false;
    }
    return credentials("wifi") && credentials("opds") && credentials("koreader");
  }

  bool profile(const std::string& source, const std::string& target, unsigned& mode) {
    mode = 0;
    const std::string path = source + "/reader_settings.bin";
    if (!Storage.exists(path.c_str())) return true;
    uint8_t raw[86];
    auto file = Storage.open(path.c_str());
    if (!file || file.size() > sizeof(raw)) return review(path, "unknown_book_profile");
    const auto size = file.size();
    if (file.read(raw, size) != int(size) || !file.close()) return false;
    if ((size != 3 || raw[0] != 1) && (size != 86 || raw[0] != 2)) return review(path, "unknown_book_profile");
    const uint8_t flags = raw[0] == 1 ? (u16(raw + 1) ? 2 : 0) : raw[1];
    const unsigned seconds = u16(raw + (raw[0] == 1 ? 1 : 2));
    if (flags & ~7 || ((flags & 2) && (seconds < 5 || seconds > 120))) return review(path, "invalid_book_profile");
    if (flags & 4) {
      mode = raw[4];
      if (mode > 2) return review(path, "unknown_render_mode");
    }
    JsonMemory globalMemory;
    JsonDocument global(&globalMemory);
    if (!io.readJson(std::string(STAGING) + "/settings.json", global)) return false;
    JsonMemory docMemory;
    JsonDocument doc(&docMemory);
    if ((flags & 1) && size == 86) {
      static constexpr const char* keys[] = {"fontFamily",
                                             "fontSize",
                                             "lineHeightPercent",
                                             "orientation",
                                             "screenMargin",
                                             "publisherPageNumbers",
                                             "paragraphAlignment",
                                             "embeddedStyle",
                                             "hyphenationEnabled",
                                             "textAntiAliasing",
                                             "readerDarkMode",
                                             "imageRendering",
                                             "extraParagraphSpacing",
                                             "forceParagraphIndents",
                                             "bionicReadingEnabled",
                                             "guideReadingEnabled",
                                             "epubRenderMode"};
      for (unsigned i = 0; i < 17; ++i) doc[keys[i]] = raw[5 + i];
      if (!memchr(raw + 22, 0, 64) || strlen(reinterpret_cast<char*>(raw + 22)) > 31)
        return review(path, "unsupported_book_font");
      doc["sdFontFamilyName"] = reinterpret_cast<const char*>(raw + 22);
      if (!convertSettings(doc, sourceVersion, sdRange, true)) return review(path, "book_text_settings_require_review");
      for (JsonPairConst field : doc.as<JsonObjectConst>()) global[field.key()] = field.value();
    }
    if (flags & 2) global["autoPageTurnSeconds"] = seconds;
    static constexpr const char* fields[] = {"fontFamily",
                                             "fontSize",
                                             "lineSpacing",
                                             "lineHeightPercent",
                                             "orientation",
                                             "screenMargin",
                                             "paragraphAlignment",
                                             "embeddedStyle",
                                             "hyphenationEnabled",
                                             "textAntiAliasing",
                                             "readerDarkMode",
                                             "imageRendering",
                                             "extraParagraphSpacing",
                                             "forceParagraphIndents",
                                             "focusReadingEnabled",
                                             "wordSpacing",
                                             "characterSpacing",
                                             "autoPageTurnSeconds",
                                             "guideReadingEnabled",
                                             "publisherPageNumbers"};
    static constexpr uint8_t defaults[] = {0, 14, 1, 0, 0, 5, 0, 1, 0, 1, 0, 0, 1, 0, 0, 100, 2, 30, 0, 0};
    uint8_t output[66]{};
    memcpy(output, "YBRS", 4);
    output[4] = 4;
    put32(output + 5, 1);
    output[9] = flags & 3;
    for (unsigned i = 0; i < 20; ++i) output[10 + i] = global[fields[i]] | defaults[i];
    const char* family = global["sdFontFamilyName"] | "";
    if (strlen(family) > 31) return review(path, "unsupported_book_font");
    strcpy(reinterpret_cast<char*>(output + 30), family);
    put32(output + 62, hash32(output, 62));
    return io.append(
        std::string(STAGING) + "/book_profiles/" + target.substr(target.rfind('/') + 1) + "/reader_profile.0", output,
        sizeof(output));
  }
  std::string section(const std::string& cache, uint16_t spine, unsigned mode) {
    static constexpr const char* suffix[] = {"", "_balanced", "_light"};
    return cache + "/sections/" + std::to_string(spine) + (mode <= 2 ? suffix[mode] : "_unsupported") + ".bin";
  }
  bool progress(Epub& epub, const std::string& source, const std::string& target, unsigned mode) {
    const std::string path = source + "/progress.bin";
    if (!Storage.exists(path.c_str())) return true;
    auto f = Storage.open(path.c_str());
    uint8_t data[10]{};
    if (!f || (f.size() != 4 && f.size() != 6 && f.size() != 10)) {
      if (f) f.close();
      review(path, "unknown_progress_format");
      return false;
    }
    const auto size = f.size();
    if (f.read(data, size) != int(size) || !f.close()) return false;
    const uint16_t spine = u16(data), page = u16(data + 2);
    if (spine >= epub.getSpineItemsCount()) {
      review(path, "missing_spine");
      return false;
    }
    uint32_t offset = size == 10 ? u32(data + 6) : 0;
    if (size != 10 && page != 0) {
      if (!anchor) {
        anchor = makeUniqueNoThrow<Anchor>();
        if (!anchor) return false;
      }
      if (!cachedLine(section(source, spine, mode), page, line, sizeof(line)) ||
          !anchor->find(epub, spine, line, offset)) {
        review(path, "reading_anchor_requires_review");
        return false;
      }
    }
    put16(data + 2, 0);
    put16(data + 4, 0);
    put32(data + 6, offset);
    if (!io.append(target + "/progress.bin", data, sizeof(data))) return false;
    ++positions;
    return true;
  }
  bool book(const std::string& path, const char* kind) {
    if (reentry) return repairBook(path, kind);
    const std::string key = std::string(kind) + "_";
    std::string source = std::string(ORIGINAL) + "/" + key + std::to_string(pathHash(path));
    if (!Storage.exists(source.c_str())) source = std::string(ORIGINAL) + "/" + key + std::to_string(gccHash(path));
    if (!Storage.exists(source.c_str())) return true;
    const bool plainText = !strcmp(kind, "txt");
    const std::string target =
        std::string(STAGING) + "/" + (plainText ? "epub_" : key) + std::to_string(std::hash<std::string>{}(path));
    if (!Storage.ensureDirectoryExists(target.c_str())) return false;
    for (const auto hash : {std::to_string(pathHash(path)), std::to_string(gccHash(path))}) {
      if (Storage.exists((std::string(ORIGINAL) + "/" + key + hash).c_str()) &&
          !io.append(std::string(STAGING) + "/migration-known-caches/" + key + hash,
                     reinterpret_cast<const uint8_t*>(path.data()), path.size()))
        return false;
    }
    // Pin the complete identity, independently of the cache hash.
    if (!io.append(target + "/migration-source-path", reinterpret_cast<const uint8_t*>(path.data()), path.size()))
      return false;
    if (plainText) {
      if (!copyTextStats(source, target)) return false;
      const std::string saved = source + "/progress.bin";
      if (!Storage.exists(saved.c_str())) return true;
      TextRecord record;
      const auto offset = readTextRecord(path, source, record) ? textAnchor(path, record) : std::nullopt;
      // Refuse to activate an incomplete store when the previous passage is
      // known to exist but cannot be mapped to the new parser.
      if (!offset) {
        review(saved, "text_anchor_requires_review");
        return false;
      }
      uint8_t result[10]{};
      put32(result + 6, *offset);
      if (!io.append(target + "/progress.bin", result, sizeof(result))) return false;
      ++positions;
      return true;
    }
    if (strcmp(kind, "epub") != 0) return fixedProgress(path, source, target);
    ++books;
    for (const char* name :
         {"stats.bin", "stats_v2.bin", "stats_v3.bin", "stats_v4.bin", "stats_v5.bin", "stats_v6.bin"}) {
      for (const char* suffix : {"", ".bak"}) {
        const std::string input = source + "/" + name + suffix;
        if (Storage.exists(input.c_str()) && !io.copy(input, target + "/" + name + suffix)) return false;
      }
    }
    unsigned mode = 0;
    if (!profile(source, target, mode)) return false;
    auto epub = makeUniqueNoThrow<Epub>(path, STAGING);
    if (!epub) {
      LOG_ERR("MIG", "Cannot allocate EPUB metadata reader");
      return false;
    }
    if (!epub->load(true, true)) return review(source, "book_cannot_be_read");
    return progress(*epub, source, target, mode);
  }
  bool fixedProgress(const std::string& path, const std::string& source, const std::string& target) {
    const std::string saved = source + "/progress.bin";
    if (!Storage.exists(saved.c_str())) return true;
    uint8_t data[4]{};
    if (readSmall(saved, data, sizeof(data)) != sizeof(data)) return review(saved, "unknown_progress_format");
    auto file = Storage.open(path.c_str());
    if (!file) return false;
    uint8_t head[8];
    const bool valid = file.size() >= 56 && readAt(file, 0, head, sizeof(head)) &&
                       (!memcmp(head, "XTC\0", 4) || !memcmp(head, "XTCH", 4)) && u32(data) < u16(head + 6);
    if (!file.close()) return false;
    if (!valid) return review(saved, "fixed_page_requires_review");
    return io.append(target + "/progress.bin", data, sizeof(data));
  }
  bool bookmarkPage(const std::string& path, float fraction, uint16_t paragraph, uint16_t& page) {
    auto file = Storage.open(path.c_str());
    uint8_t h[44];
    if (!file || !readAt(file, 0, h, sizeof(h))) return false;
    const bool modern = !memcmp(h,
                                "\xff"
                                "CXS\x2c",
                                5);
    if (!modern && h[0] != 27) return false;
    const auto count = u16(h + (modern ? 26 : 19));
    if (!count) return false;
    page = std::min<unsigned>(unsigned(fraction * count), count - 1);
    if (paragraph != UINT16_MAX) {
      const uint32_t lut = u32(h + (modern ? 36 : 29));
      if (lut < (modern ? 44u : 37u) || !readAt(file, lut, h, 2) || u16(h) != count ||
          uint64_t(lut) + 2 + count * 2 > file.size())
        return false;
      uint16_t previous = 0;
      bool found = false;
      page = count - 1;
      for (unsigned i = 0; i < count; ++i) {
        if (!readAt(file, lut + 2 + i * 2, h, 2) || u16(h) < previous) return false;
        previous = u16(h);
        if (!found && previous >= paragraph) {
          page = i;
          found = true;
        }
      }
    }
    return file.close();
  }
  bool bookmarkFile(const std::string& source) {
    auto file = Storage.open(source.c_str());
    uint8_t record[124]{};
    if (!file || !readAt(file, 0, record, 3)) return review(source, "invalid_bookmarks");
    const unsigned version = record[0];
    if (version < 2 || version > 5) return review(source, "unknown_bookmark_version");
    const unsigned count = version == 2 ? record[1] : u16(record + 1);
    size_t cursor = version == 2 ? 2 : 3;
    if (count > 1024) return review(source, "too_many_bookmarks");
    std::string path;
    for (unsigned i = 0; i < 3; ++i) {
      if (!readAt(file, cursor, record, 4)) return review(source, "invalid_bookmark_header");
      cursor += 4;
      const size_t length = u32(record);
      if (length > 65536 || cursor > file.size() || length > file.size() - cursor)
        return review(source, "invalid_bookmark_header");
      if (i == 2) {
        if (length >= sizeof(line) || !readAt(file, cursor, reinterpret_cast<uint8_t*>(line), length) ||
            memchr(line, 0, length))
          return review(source, "invalid_bookmark_path");
        line[length] = 0;
        path = line;
      }
      cursor += length;
    }
    const unsigned recordSize = version == 5 ? 124 : version == 4 ? 60 : 58;
    if (!pathSafe(path.c_str()) || path.size() < 6 || strcasecmp(path.c_str() + path.size() - 5, ".epub") ||
        cursor + size_t(recordSize) * count != file.size())
      return review(source, "invalid_bookmark_identity");
    if (!file.close()) return false;
    if (!Storage.exists(path.c_str())) return review(source, "bookmarked_book_missing");
    std::string oldCache = std::string(ORIGINAL) + "/epub_" + std::to_string(pathHash(path));
    if (!Storage.exists(oldCache.c_str())) oldCache = std::string(ORIGINAL) + "/epub_" + std::to_string(gccHash(path));
    unsigned mode = 0;
    auto profileFile = Storage.open((oldCache + "/reader_settings.bin").c_str());
    if (profileFile) {
      if (readAt(profileFile, 0, record, 5) && record[0] == 2 && (record[1] & 4)) mode = record[4];
      profileFile.close();
    }
    auto epub = makeUniqueNoThrow<Epub>(path, STAGING);
    if (!epub) return false;
    if (!epub->load(true, true)) return review(source, "bookmarked_book_unreadable");
    const std::string relative = BookmarkUtil::getBookmarkPath(path).substr(strlen("/.crosspoint/"));
    const std::string destination = std::string(STAGING) + "/" + relative;
    // Lowercase identity pins also detect collisions on the native case-sensitive test filesystem.
    std::string folded = relative;
    for (char& c : folded)
      if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    const std::string owner = std::string(STAGING) + "/migration-bookmark-owners/" + folded.substr(10);
    if (!io.append(owner, reinterpret_cast<const uint8_t*>(source.data()), source.size())) {
      LOG_ERR("MIG", "Bookmark filename collision; migration paused");
      return false;
    }
    const std::string scratch =
        std::string(STAGING) + "/migration-bookmark-staging/" + std::to_string(pathHash(source));
    static constexpr char start[] = "{\"bookmarks\":[";
    if (!io.fragment(scratch, 0, reinterpret_cast<const uint8_t*>(start), sizeof(start) - 1)) return false;
    size_t written = sizeof(start) - 1;
    for (unsigned i = 0; i < count; ++i) {
      file = Storage.open(source.c_str());
      if (!file || !readAt(file, cursor + i * recordSize, record, recordSize) || !file.close()) return false;
      const uint16_t spine = u16(record);
      float fraction;
      memcpy(&fraction, record + 2, sizeof(fraction));
      if (spine >= epub->getSpineItemsCount() || !std::isfinite(fraction) || fraction < 0 || fraction > 1)
        return review(source, "invalid_bookmark_position");
      uint32_t offset = 0;
      if (!anchor) anchor = makeUniqueNoThrow<Anchor>();
      if (!anchor) return false;
      bool found = false;
      if (version == 5 && memchr(record + 60, 0, 64) && record[60]) {
        strcpy(line, reinterpret_cast<char*>(record + 60));
        found = anchor->find(*epub, spine, line, offset);
      }
      if (!found) {
        uint16_t page;
        const auto chapter = section(oldCache, spine, mode);
        if (!bookmarkPage(chapter, fraction, version >= 4 ? u16(record + 58) : UINT16_MAX, page) ||
            !cachedLine(chapter, page, line, sizeof(line)) || !anchor->find(*epub, spine, line, offset))
          return review(source, "bookmark_anchor_requires_review");
      }
      JsonMemory memory;
      JsonDocument entry(&memory);
      entry["xpath"] = "";
      entry["percentage"] = epub->calculateProgress(spine, fraction);
      entry["summary"] = line;
      entry["name"] = "";
      entry["si"] = spine;
      entry["pc"] = 0;
      entry["pp"] = 0;
      entry["vo"] = offset;
      const size_t size = measureJson(entry);
      if (entry.overflowed() || size > 8192) return false;
      auto json = makeUniqueNoThrow<uint8_t[]>(size + 1);
      if (!json) return false;
      if (i && !io.fragment(scratch, written++, reinterpret_cast<const uint8_t*>(","), 1)) return false;
      if (serializeJson(entry, json.get(), size + 1) != size || !io.fragment(scratch, written, json.get(), size))
        return false;
      written += size;
    }
    if (!io.fragment(scratch, written, reinterpret_cast<const uint8_t*>("]}"), 2)) return false;
    file = Storage.open(scratch.c_str());
    if (!file || file.size() != written + 2 || !file.close()) return false;
    // A failed anchor leaves only an inactive scratch file, never half a user's bookmarks.
    return io.copy(scratch, destination);
  }
  bool bookmarks() {
    const std::string folder = std::string(ORIGINAL) + "/bookmarks";
    if (!Storage.exists(folder.c_str())) return true;
    auto dir = Storage.open(folder.c_str());
    if (!dir || !dir.isDirectory()) return false;
    size_t position = 0;
    while (true) {
      if (!dir.seekSet(position)) return false;
      auto item = dir.openNextFile();
      if (!item) break;
      const bool directory = item.isDirectory();
      if (!item.getName(name, sizeof(name))) return false;
      const std::string leaf = name;
      position = dir.position();
      item.close();
      dir.close();
      const char* extension = strrchr(leaf.c_str(), '.');
      if (!directory && extension && !strcmp(extension, ".bin")) {
        if (!bookmarkFile(folder + "/" + leaf)) return false;
      } else if (!directory && extension && !strcmp(extension, ".json")) {
        // Preserve bookmarks created while trying the earlier CrossPoint build.
        if (!io.copy(folder + "/" + leaf, std::string(STAGING) + "/bookmarks/" + leaf)) return false;
      }
      dir = Storage.open(folder.c_str());
      if (!dir) return false;
    }
    return dir.close();
  }
  bool scan(const std::string& directory, unsigned depth = 0) {
    if (depth > 12 || directory.size() > 480) return review(directory, "book_path_too_deep");
    auto dir = Storage.open(directory.c_str());
    if (!dir || !dir.isDirectory()) return false;
    uint32_t position = 0;
    while (true) {
      if (!dir.seekSet(position)) return false;
      auto item = dir.openNextFile();
      if (!item) break;
      const bool isDir = item.isDirectory();
      if (!item.getName(name, sizeof(name))) return false;
      const std::string leaf = name;
      position = dir.position();
      item.close();
      dir.close();
      const std::string path = (directory == "/" ? "" : directory) + "/" + leaf;
      if (!leaf.empty() && leaf[0] != '.' && leaf != "System Volume Information" && pathSafe(path.c_str())) {
        if (isDir) {
          if (!scan(path, depth + 1)) return false;
        } else {
          const char* extension = strrchr(leaf.c_str(), '.');
          const char* kind = extension && !strcasecmp(extension, ".epub")                                     ? "epub"
                             : extension && (!strcasecmp(extension, ".txt") || !strcasecmp(extension, ".md")) ? "txt"
                             : extension && (!strcasecmp(extension, ".xtc") || !strcasecmp(extension, ".xtch"))
                                 ? "xtc"
                                 : nullptr;
          if (kind && !book(path, kind)) return false;
        }
      }
      dir = Storage.open(directory.c_str());
      if (!dir) return false;
      delay(1);
    }
    return dir.close();
  }
  bool unknownCaches() {
    auto dir = Storage.open(ORIGINAL);
    if (!dir) return false;
    size_t position = 0;
    while (true) {
      if (!dir.seekSet(position)) return false;
      auto item = dir.openNextFile();
      if (!item) break;
      const bool directory = item.isDirectory();
      if (!item.getName(name, sizeof(name))) return false;
      const std::string leaf = name;
      position = dir.position();
      item.close();
      dir.close();
      if (directory && (leaf.rfind("epub_", 0) == 0 || leaf.rfind("txt_", 0) == 0 || leaf.rfind("xtc_", 0) == 0) &&
          !Storage.exists((std::string(STAGING) + "/migration-known-caches/" + leaf).c_str()) &&
          Storage.exists((std::string(ORIGINAL) + "/" + leaf + "/progress.bin").c_str()) &&
          !review(std::string(ORIGINAL) + "/" + leaf, "book_path_not_identified"))
        return false;
      dir = Storage.open(ORIGINAL);
      if (!dir) return false;
    }
    return dir.close();
  }
  bool identifySource(bool& isYacp) {
    // Reuse the bounded boot-only JSON reader; release the document before
    // preparing settings or scanning books. No extra persistent buffer.
    JsonMemory memory;
    JsonDocument doc(&memory);
    const char* path = Storage.exists(ORIGINAL) ? "/.yacp-migration-v1/original/crossink-settings.json"
                                                : "/.crosspoint/crossink-settings.json";
    if (!io.readJson(path, doc) || !doc.is<JsonObject>()) {
      LOG_ERR("MIG", "Cannot identify legacy settings source; data retained");
      return false;
    }
    const char* version = doc["buttonLayoutPromptVersion"] | "";
    isYacp = strstr(version, "-yacp") != nullptr;
    return true;
  }
  bool run() {
    const std::string done = std::string(ROOT) + "/complete";
    if (marker(done)) return requireUpgradeBackup() && recoverCurrent();
    const std::string ready = std::string("/.crosspoint/") + "migration-ready";
    if (marker(ready)) return io.append(done, MARK, sizeof(MARK)) && recoverCurrent();
    if (Storage.exists(done.c_str())) return false;
    if (!Storage.exists(ROOT) && !Storage.exists("/.crosspoint") && !Storage.exists(SOURCE_BACKUP_PENDING) &&
        !Storage.exists(UPGRADE_BACKUP_PENDING))
      return true;
    if (!Storage.exists(ROOT) && !Storage.exists(CROSSINK_SETTINGS))
      return requireSourceBackup() && recoverNativeTextIfPresent();
    bool sourceIsYacp = false;
    if (!identifySource(sourceIsYacp)) return false;
    if (!sourceIsYacp) {
      if (Storage.exists(ORIGINAL) || Storage.exists((std::string(ROOT) + "/intent").c_str())) {
        LOG_ERR("MIG", "Pending transaction has no YACP source identity; original retained");
        return false;
      }
      // A leftover CrossInk store must not replace an existing native profile.
      if (!requireSourceBackup() || !importCrossInkSettings() || !nativeWifiCredentials()) return false;
      checkpoint("native-settings");
      return true;
    }
    if (!Storage.exists(ORIGINAL)) {
      if (!Storage.exists("/.crosspoint/crossink-settings.json")) return false;
      if (!Storage.ensureDirectoryExists(ROOT) || !io.append(std::string(ROOT) + "/intent", MARK, sizeof(MARK)))
        return false;
      if (!Storage.rename("/.crosspoint", ORIGINAL)) return false;
      checkpoint("original");
    }
    if (!marker(std::string(ROOT) + "/intent") || Storage.exists("/.crosspoint")) return false;
    if (!Storage.ensureDirectoryExists(STAGING) || !globals() || !scan("/") || !bookmarks() || !unknownCaches())
      return false;
    checkpoint("prepared");
    JsonMemory reportMemory;
    JsonDocument report(&reportMemory);
    report["schema"] = 1;
    report["books"] = books;
    report["positions"] = positions;
    report["reviews"] = reviews;
    report["original"] = ORIGINAL;
    if (!io.writeJson(std::string(STAGING) + "/migration-report.json", report) ||
        !io.append(std::string(STAGING) + "/migration-ready", MARK, sizeof(MARK)) ||
        !Storage.rename(STAGING, "/.crosspoint"))
      return false;
    checkpoint("activated");
    if (!io.append(done, MARK, sizeof(MARK))) return false;
    LOG_INF("MIG", "Migration complete: %u books, %u positions, %u review records; originals retained", books,
            positions, reviews);
    return true;
  }
};
}  // namespace
}  // namespace legacy

bool LegacyMigration::run(bool checkedDeepSleepResume) {
#ifdef SIMULATOR
  checkedDeepSleepResume = checkedDeepSleepResume || std::getenv("YACP_TEST_DEEP_RESUME");
#endif
  const std::string checked = legacy::CHECKED;
  if (checkedDeepSleepResume && !Storage.exists(legacy::Migration::REPAIR) &&
      !Storage.exists(legacy::Migration::SOURCE_BACKUP_PENDING) && legacy::marker(checked)) {
    LOG_DBG("MIG", "Checked deep-sleep resume");
    return true;
  }
  // About 4 KiB for file-copy buffers and path/text scratch, released before
  // ordinary font, reader and network initialization. Anchor storage is lazy.
  auto migration = makeUniqueNoThrow<legacy::Migration>();
  if (!migration) {
    LOG_ERR("MIG", "Cannot allocate migration workspace");
    legacy::failed = true;
    return false;
  }
  legacy::failed = !(Storage.exists(legacy::Migration::REPAIR) ? migration->recoverCurrent() : migration->run());
  if (!legacy::failed && !Storage.exists((std::string("/.crosspoint") + legacy::BOOK_MOVE).c_str()) &&
      !legacy::marker(checked)) {
    legacy::failed = !migration->io.append(checked, legacy::MARK, sizeof(legacy::MARK));
  }
  if (legacy::failed) LOG_ERR("MIG", "Migration paused; original data retained. Restart to retry");
  return !legacy::failed;
}
bool LegacyMigration::repairTextAfterBookMove() {
  auto migration = makeUniqueNoThrow<legacy::Migration>();
  if (!migration) {
    LOG_ERR("MIG", "Cannot allocate migration workspace after book move");
    legacy::failed = true;
    return false;
  }
  // Settings and recents are already loaded for book move recovery. Limit
  // this pass to positions and per-book statistics.
  migration->textOnly = true;
  legacy::failed = !migration->recoverNativeTextIfPresent() ||
                   !migration->io.append(legacy::CHECKED, legacy::MARK, sizeof(legacy::MARK));
  if (legacy::failed) LOG_ERR("MIG", "Plain text migration paused after book move; data retained");
  return !legacy::failed;
}
bool LegacyMigration::blocked() { return legacy::failed; }
bool LegacyMigration::sourceBackupFailed() { return legacy::backupFailed; }
