#include "DictionaryHistory.h"

#include <CredentialIntegrity.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "StringUtils.h"

namespace {

constexpr auto TAG = "DHI";
constexpr auto ROOT = "/.crosspoint/dictionary-history";
constexpr size_t MAX_BYTES = 8192;
using DictionaryHistory::MAX_WORDS;
constexpr size_t MAX_WORD_BYTES = 255;

struct Header {
  uint32_t magic;
  uint32_t generation;
  uint32_t size;
  uint32_t crc;
};
constexpr uint32_t MAGIC = 0x31484944;  // DIH1

class Snapshot {
  std::unique_ptr<char[]> data = makeUniqueNoThrow<char[]>(MAX_BYTES);
  std::string directory;
  uint32_t generation = 0;
  size_t size = 0;
  int activeSlot = 1;

  std::string path(int slot) const { return directory + (slot == 0 ? "/a.bin" : "/b.bin"); }

  uint32_t checksum(const Header& header) const {
    return credential_integrity::crc32({reinterpret_cast<const char*>(&header), 3 * sizeof(uint32_t)}) ^
           credential_integrity::crc32({data.get(), header.size});
  }

  bool readSlot(int slot, uint32_t& version) {
    auto file = Storage.open(path(slot).c_str());
    if (!file) return false;
    Header header{};
    const bool valid = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == MAGIC &&
                       header.size <= MAX_BYTES && file.size() == sizeof(header) + header.size &&
                       file.read(data.get(), header.size) == static_cast<int>(header.size) &&
                       checksum(header) == header.crc;
    file.close();
    if (!valid) return false;

    int count = 0;
    for (size_t offset = 0; offset < header.size;) {
      const auto* end = static_cast<const char*>(memchr(data.get() + offset, '\0', header.size - offset));
      if (!end || end == data.get() + offset || static_cast<size_t>(end - (data.get() + offset)) > MAX_WORD_BYTES ||
          ++count > MAX_WORDS) {
        return false;
      }
      offset = end - data.get() + 1;
    }
    size = header.size;
    version = header.generation;
    return true;
  }

 public:
  bool load(const char* dictionary) {
    if (!data || !dictionary || !*dictionary || dictionary[0] == '.' || strpbrk(dictionary, "/\\:")) {
      LOG_ERR(TAG, "Cannot allocate history or invalid dictionary name");
      return false;
    }
    directory = std::string(ROOT) + "/" + dictionary;

    const bool hasA = Storage.exists(path(0).c_str());
    const bool hasB = Storage.exists(path(1).c_str());
    uint32_t a = 0;
    uint32_t b = 0;
    const bool validA = hasA && readSlot(0, a);
    const bool validB = hasB && readSlot(1, b);
    if (validB && (!validA || static_cast<int32_t>(b - a) > 0)) {
      generation = b;
      return true;
    }
    if (validA) {
      // Slot B may have overwritten the scratch buffer even if its CRC failed.
      if (hasB && !readSlot(0, a)) return false;
      activeSlot = 0;
      generation = a;
      return true;
    }
    size = 0;
    if (hasA && hasB) {
      LOG_ERR(TAG, "Both dictionary history copies are invalid");
      return false;
    }
    // An interrupted first save leaves at most one invalid copy.
    activeSlot = hasB ? 0 : 1;
    return true;
  }

  bool save() const {
    if (!Storage.ensureDirectoryExists(directory.c_str())) return false;
    // The previous valid copy stays intact until this copy has been written and closed.
    auto file = Storage.open(path(1 - activeSlot).c_str(), O_WRITE | O_CREAT | O_TRUNC);
    if (!file) return false;
    Header header{MAGIC, generation + 1, static_cast<uint32_t>(size), 0};
    header.crc = checksum(header);
    const bool written = file.write(&header, sizeof(header)) == sizeof(header) && file.write(data.get(), size) == size;
    const bool closed = file.close();
    return written && closed;
  }

  size_t offsetAt(int index) const {
    size_t offset = 0;
    while (index-- > 0 && offset < size) offset += strlen(data.get() + offset) + 1;
    return offset;
  }

  const char* at(int index) const {
    if (index < 0) return nullptr;
    const auto offset = offsetAt(index);
    return offset < size ? data.get() + offset : nullptr;
  }

  int count() const { return std::count(data.get(), data.get() + size, '\0'); }

  void takeWords(DictionaryHistory::WordList& words) {
    words.count = 0;
    for (size_t offset = 0; offset < size; offset += strlen(data.get() + offset) + 1) {
      words.offsets[words.count++] = static_cast<uint16_t>(offset);
    }
    words.text = std::move(data);
  }

  void erase(int index) {
    const auto offset = offsetAt(index);
    const auto next = offset + strlen(data.get() + offset) + 1;
    memmove(data.get() + offset, data.get() + next, size - next);
    size -= next - offset;
  }

  void insert(int index, const std::string& word) {
    const size_t bytes = word.size() + 1;
    int retained = count();
    while (retained >= MAX_WORDS || size + bytes > MAX_BYTES) {
      size = offsetAt(--retained);
    }
    const auto offset = offsetAt(index);
    memmove(data.get() + offset + bytes, data.get() + offset, size - offset);
    memcpy(data.get() + offset, word.c_str(), bytes);
    size += bytes;
  }
};

bool save(const Snapshot& snapshot) {
  if (snapshot.save()) return true;
  LOG_ERR(TAG, "Could not write dictionary history");
  return false;
}

}  // namespace

namespace DictionaryHistory {

void record(const char* dictionary, const std::string& word) {
  if (word.empty() || word.size() > MAX_WORD_BYTES) return;
  Snapshot snapshot;
  if (!snapshot.load(dictionary)) return;
  for (int i = 0; const char* entry = snapshot.at(i); ++i) {
    if (StringUtils::asciiCaseCmp(entry, word.c_str()) != 0) continue;
    if (i == 0 && word == entry) return;
    snapshot.erase(i);
    break;
  }
  snapshot.insert(0, word);
  save(snapshot);
}

bool read(const char* dictionary, int index, std::string& word, int& count) {
  Snapshot snapshot;
  if (!snapshot.load(dictionary)) return false;
  count = snapshot.count();
  const char* entry = snapshot.at(std::clamp(index, 0, std::max(0, count - 1)));
  word = entry ? entry : "";
  return true;
}

bool readList(const char* dictionary, WordList& words) {
  Snapshot snapshot;
  if (!snapshot.load(dictionary)) return false;
  snapshot.takeWords(words);
  return true;
}

bool remove(const char* dictionary, int index) {
  Snapshot snapshot;
  if (!snapshot.load(dictionary) || !snapshot.at(index)) return false;
  snapshot.erase(index);
  return save(snapshot);
}

bool restore(const char* dictionary, int index, const std::string& word) {
  if (word.empty() || word.size() > MAX_WORD_BYTES) return false;
  Snapshot snapshot;
  if (!snapshot.load(dictionary)) return false;
  snapshot.insert(index, word);
  return save(snapshot);
}

}  // namespace DictionaryHistory
