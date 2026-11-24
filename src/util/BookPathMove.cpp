#include "BookPathMove.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <atomic>
#include <cstring>
#include <functional>

#include "BookGallery.h"
#include "BookmarkUtil.h"
#include "ClippingStore.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "activities/reader/FinishedBooksIndex.h"

namespace {
constexpr char JOURNAL[] = "/.crosspoint/book-move.bin";
constexpr size_t PATH_SIZE = 512;
constexpr uint8_t JOURNAL_VERSION = 3;
constexpr size_t CLIPPING_PAIR_START = 7;
constexpr size_t PAIRS = CLIPPING_PAIR_START + ClippingStore::FILE_SUFFIXES.size();
constexpr uint16_t V1_PAIR_MASK = 0x1f;
constexpr uint16_t V2_PAIR_MASK = 0x7f;
constexpr uint16_t V3_PAIR_MASK = (1u << PAIRS) - 1;
constexpr size_t HEADER_SIZE = 24;
constexpr size_t RECORD_SIZE = HEADER_SIZE + PATH_SIZE * 2 + 4;
constexpr const char* METADATA[] = {"/.crosspoint/recent.json", "/.crosspoint/state.json"};
constexpr const char* SNAPSHOTS[] = {"/.crosspoint/book-move-recent.json", "/.crosspoint/book-move-state.json"};
std::atomic<bool> recoveryBlocked{false};
std::atomic<bool> moveInProgress{false};
struct Plan {
  uint8_t record[RECORD_SIZE] = {};
  std::string source[PAIRS];
  std::string target[PAIRS];
};
uint16_t sourcePresence(const Plan& plan) { return uint16_t(plan.record[5]) | (uint16_t(plan.record[7]) << 8); }

// One cold-path allocation for the intent and associated paths. Keep it off the
// task stack and free it before any reader opens.
std::unique_ptr<Plan> allocatePlan() {
  auto plan = makeUniqueNoThrow<Plan>();
  if (!plan) LOG_ERR("MOVE", "OOM: book move plan");
  return plan;
}
uint32_t checksum(const uint8_t* bytes, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void write32(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (i * 8));
}
bool digest(const char* path, uint32_t& size, uint32_t& hash) {
  HalFile file;
  if (!Storage.openFileForRead("MOVE", path, file)) return false;
  size = file.size();
  hash = 2166136261u;
  if (size > 256 * 1024) { file.close(); return false; }
  uint8_t bytes[128];
  for (uint32_t remaining = size; remaining;) {
    const auto count = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
    if (file.read(bytes, count) != count) { file.close(); return false; }
    for (size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    remaining -= count;
  }
  file.close();
  return true;
}
bool copyChecked(const char* source, const char* target, uint32_t size, uint32_t hash) {
  uint32_t actualSize, actualHash;
  if (!digest(source, actualSize, actualHash) || actualSize != size || actualHash != hash) return false;
  if (Storage.exists(target) && digest(target, actualSize, actualHash) && actualSize == size && actualHash == hash) return true;
  HalFile from, to;
  if (!Storage.openFileForRead("MOVE", source, from)) return false;
  if (!Storage.openFileForWrite("MOVE", target, to)) { from.close(); return false; }
  uint8_t bytes[128];
  bool valid = true;
  for (uint32_t remaining = size; remaining;) {
    const auto count = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
    if (from.read(bytes, count) != count || to.write(bytes, count) != count) { valid = false; break; }
    remaining -= count;
  }
  from.close();
  const bool closed = to.close();
  return valid && closed && digest(target, actualSize, actualHash) && actualSize == size && actualHash == hash;
}
bool validPath(const std::string& path) {
  if (path.size() < 2 || path.size() >= PATH_SIZE || path.front() != '/' || path.back() == '/') return false;
  size_t begin = 1;
  while (begin < path.size()) {
    const auto end = path.find('/', begin);
    const auto length = (end == std::string::npos ? path.size() : end) - begin;
    if (!length || path[begin] == '.') return false;  // No system or relative components.
    for (size_t i = begin; i < begin + length; ++i) {
      const unsigned char c = path[i];
      if (c < 32 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    }
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return true;
}
bool sameFatName(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}
const char* legacyCachePrefix(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) return "epub_";
  if (FsHelpers::hasXtcExtension(path)) return "xtc_";
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) return "txt_";
  return nullptr;
}
bool derive(Plan& plan) {
  if (!validPath(plan.source[0]) || !validPath(plan.target[0]) || sameFatName(plan.source[0], plan.target[0])) return false;
  const char* prefix = legacyCachePrefix(plan.source[0]);
  const char* targetPrefix = legacyCachePrefix(plan.target[0]);
  if ((prefix == nullptr) != (targetPrefix == nullptr) || (prefix && strcmp(prefix, targetPrefix))) return false;
  if (!prefix) return true;  // Ordinary file rename still gets interruption recovery.
  // Version 1 journals were written by the stand-alone TXT reader.
  const bool reflowable = strcmp(prefix, "epub_") == 0 ||
                          (plan.record[4] >= 2 && strcmp(prefix, "txt_") == 0);
  if (reflowable) prefix = "epub_";
  for (unsigned side = 0; side < 2; ++side) {
    auto* paths = side ? plan.target : plan.source;
    const auto hash = std::to_string(std::hash<std::string>{}(paths[0]));
    paths[1] = std::string("/.crosspoint/") + prefix + hash;
    if (reflowable) {
      paths[2] = BookmarkUtil::getBookmarkPath(paths[0]);
      paths[3] = "/.crosspoint/book_profiles/epub_" + hash;
    }
    char gallery[BookGallery::FOLDER_CAPACITY];
    if (!BookGallery::folderForBook(paths[0], gallery)) return false;
    paths[4] = gallery;
    if (plan.record[4] >= 2 && reflowable) {
      paths[5] = paths[0] + ".key";
      paths[6] = paths[0] + ".rights";
    }
    if (plan.record[4] >= 3 && reflowable) {
      const auto clipping = ClippingStore::filePathForBook(paths[0], "epub");
      for (size_t i = 0; i < ClippingStore::FILE_SUFFIXES.size(); ++i) {
        paths[CLIPPING_PAIR_START + i] = clipping + ClippingStore::FILE_SUFFIXES[i];
      }
    }
  }
  return true;
}
bool readRecord(Plan& plan) {
  HalFile file;
  if (!Storage.openFileForRead("MOVE", JOURNAL, file)) return false;
  const bool read = file.size() == RECORD_SIZE && file.read(plan.record, RECORD_SIZE) == RECORD_SIZE;
  file.close();
  const auto version = plan.record[4];
  const uint16_t pairMask = version == 1 ? V1_PAIR_MASK : version == 2 ? V2_PAIR_MASK : V3_PAIR_MASK;
  const uint16_t presence = sourcePresence(plan);
  if (!read || memcmp(plan.record, "YBM1", 4) || version < 1 || version > JOURNAL_VERSION || !(presence & 1) ||
      (presence & ~pairMask) || (plan.record[6] & ~3u)) return false;
  const auto sum = checksum(plan.record, RECORD_SIZE - 4);
  for (unsigned i = 0; i < 4; ++i) if (plan.record[RECORD_SIZE - 4 + i] != uint8_t(sum >> (8 * i))) return false;
  for (unsigned side = 0; side < 2; ++side) {
    const char* path = reinterpret_cast<const char*>(plan.record + HEADER_SIZE + side * PATH_SIZE);
    if (!memchr(path, 0, PATH_SIZE)) return false;
    (side ? plan.target : plan.source)[0] = path;
  }
  return derive(plan);
}
bool inspect(const Plan& plan, bool initial) {
  const uint16_t presence = sourcePresence(plan);
  for (size_t i = 0; i < PAIRS; ++i) {
    const bool expected = presence & (1u << i);
    const bool from = !plan.source[i].empty() && Storage.exists(plan.source[i].c_str());
    const bool to = !plan.target[i].empty() && Storage.exists(plan.target[i].c_str());
    if (initial ? to : expected ? from == to : from || to) return false;
    if (expected && (plan.source[i].empty() || plan.target[i].empty() || sameFatName(plan.source[i], plan.target[i]))) return false;
  }
  return true;
}
bool complete(Plan& plan) {
  if (!inspect(plan, false)) {
    LOG_ERR("MOVE", "Ambiguous source/target state; journal and all data retained");
    return false;
  }
  // Book last: never expose the destination book before its associated files.
  const uint16_t presence = sourcePresence(plan);
  for (size_t step = 1; step <= PAIRS; ++step) {
    const size_t i = step % PAIRS;
    if (!(presence & (1u << i)) || !Storage.exists(plan.source[i].c_str())) continue;
    if (!Storage.rename(plan.source[i].c_str(), plan.target[i].c_str())) {
      LOG_ERR("MOVE", "Rename failed; durable recovery intent retained (part %u)", unsigned(i));
      return false;
    }
  }
  if (!FinishedBooksIndex::migratePath(plan.source[0], plan.target[0]) ||
      !RECENT_BOOKS.updatePath(plan.source[0], plan.target[0], plan.source[1], plan.target[1])) return false;
  if (APP_STATE.openEpubPath == plan.source[0]) {
    APP_STATE.openEpubPath = plan.target[0];
    if (!APP_STATE.saveToFile()) {
      APP_STATE.openEpubPath = plan.source[0];
      LOG_ERR("MOVE", "Could not persist open-book path");
      return false;
    }
  }
  if (!Storage.remove(JOURNAL)) {
    LOG_ERR("MOVE", "Could not retire completed move journal");
    return false;
  }
  LOG_INF("MOVE", "Book and associated data moved successfully");
  return true;
}
}  // namespace

bool BookPathMove::hasPending() { return Storage.exists(JOURNAL); }

bool BookPathMove::blocked() { return recoveryBlocked; }

bool BookPathMove::prepareRecovery() {
  if (!Storage.exists(JOURNAL)) { recoveryBlocked = false; return true; }
  recoveryBlocked = true;
  auto plan = allocatePlan();
  if (!plan || !readRecord(*plan)) return false;
  for (unsigned i = 0; i < 2; ++i) {
    if (!(plan->record[6] & (1u << i))) continue;
    if (!copyChecked(SNAPSHOTS[i], METADATA[i], read32(plan->record + 8 + i * 8),
                     read32(plan->record + 12 + i * 8))) {
      LOG_ERR("MOVE", "Metadata snapshot restore failed; original snapshot retained");
      return false;
    }
  }
  return true;
}

bool BookPathMove::recover() {
  if (!Storage.exists(JOURNAL)) { recoveryBlocked = false; return true; }
  recoveryBlocked = true;
  auto plan = allocatePlan();
  if (!plan || !readRecord(*plan)) {
    LOG_ERR("MOVE", "Invalid or unreadable move journal; data retained for recovery");
    return false;
  }
  if (!complete(*plan)) return false;
  recoveryBlocked = false;
  return true;
}

bool BookPathMove::move(const std::string& source, const std::string& target) {
  if (moveInProgress.exchange(true)) {
    LOG_ERR("MOVE", "Another move is still in progress");
    return false;
  }
  const ScopedCleanup releaseMove([] { moveInProgress.store(false); });
  if (recoveryBlocked || Storage.exists(JOURNAL)) {
    LOG_ERR("MOVE", "Unfinished move must be recovered before a new operation");
    recoveryBlocked = true;
    return false;
  }
  auto plan = allocatePlan();
  if (!plan) return false;
  plan->source[0] = source;
  plan->target[0] = target;
  plan->record[4] = JOURNAL_VERSION;
  if (!derive(*plan) || !Storage.exists(source.c_str()) || !inspect(*plan, true) ||
      !FinishedBooksIndex::canMigratePath(source, target)) {
    LOG_ERR("MOVE", "Invalid path or existing destination data; nothing moved");
    return false;
  }
  auto book = Storage.open(source.c_str());
  const bool isFile = book && !book.isDirectory();
  book.close();
  if (!isFile) return false;
  memcpy(plan->record, "YBM1", 4);
  uint16_t presence = 0;
  for (size_t i = 0; i < PAIRS; ++i) {
    if (!plan->source[i].empty() && Storage.exists(plan->source[i].c_str())) presence |= 1u << i;
  }
  plan->record[5] = uint8_t(presence);
  plan->record[7] = uint8_t(presence >> 8);
  if (!inspect(*plan, false)) { LOG_ERR("MOVE", "Colliding book identities; nothing moved"); return false; }
  // Immutable copies protect all fields if a normal JSON-store write is torn.
  // Keep them after success; the next move may replace them only with no journal.
  for (unsigned i = 0; i < 2; ++i) {
    if (!Storage.exists(METADATA[i])) continue;
    uint32_t size, hash;
    if (!digest(METADATA[i], size, hash) || !copyChecked(METADATA[i], SNAPSHOTS[i], size, hash)) {
      LOG_ERR("MOVE", "Cannot preserve metadata; nothing moved");
      return false;
    }
    plan->record[6] |= 1u << i;
    write32(plan->record + 8 + i * 8, size);
    write32(plan->record + 12 + i * 8, hash);
  }
  memcpy(plan->record + HEADER_SIZE, source.c_str(), source.size());
  memcpy(plan->record + HEADER_SIZE + PATH_SIZE, target.c_str(), target.size());
  const auto sum = checksum(plan->record, RECORD_SIZE - 4);
  for (unsigned i = 0; i < 4; ++i) plan->record[RECORD_SIZE - 4 + i] = uint8_t(sum >> (8 * i));
  HalFile file;
  if (!Storage.openFileForWrite("MOVE", JOURNAL, file)) {
    recoveryBlocked = Storage.exists(JOURNAL);
    return false;
  }
  const bool written = file.write(plan->record, RECORD_SIZE) == RECORD_SIZE;
  const bool closed = file.close();
  // Reopen and validate the durable record before the first rename.
  if (!written || !closed || !readRecord(*plan) || !complete(*plan)) {
    recoveryBlocked = true;
    LOG_ERR("MOVE", "Move interrupted; restart to recover before further storage operations");
    return false;
  }
  recoveryBlocked = false;
  return true;
}
