#include "SyncedStatsStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nearby_stats {
namespace {
constexpr const char* DIRECTORY = "/.crosspoint/synced_stats";
enum class Read { Missing, Valid, Invalid, Future };
Read read(const char* path, uint8_t* data, size_t& size) {
  size = 0;
  if (!Storage.exists(path)) return Read::Missing;
  HalFile file;
  if (!Storage.openFileForRead("NSYNC",path,file)) return Read::Invalid;
  const size_t length = file.fileSize();
  const size_t amount = std::min(length,STATS_SIZE);
  const int count = file.read(data,amount);
  const bool closed = file.close();
  if (!closed || count != static_cast<int>(amount) || !amount) return Read::Invalid;
  if (length > STATS_SIZE || data[0] > 3) return Read::Future;
  if (!validStats(data,length)) return Read::Invalid;
  size = length;
  return Read::Valid;
}

bool matches(const char* path, const uint8_t* data, size_t size) {
  HalFile file;
  if (!Storage.openFileForRead("NSYNC",path,file)) return false;
  bool equal = file.fileSize() == size;
  uint8_t chunk[48];
  for (size_t offset = 0; equal && offset < size;) {
    const size_t amount = std::min(sizeof(chunk),size - offset);
    equal = file.read(chunk,amount) == static_cast<int>(amount) && !memcmp(chunk,data + offset,amount);
    offset += amount;
  }
  return file.close() && equal;
}

bool replaceable(const char* path) {
  uint8_t prior[STATS_SIZE];
  size_t size;
  const Read result = read(path,prior,size);
  return result == Read::Missing || result == Read::Valid;
}
}  // namespace

bool readLocalSnapshot(uint8_t* data, size_t& size) {
  if (!data) return false;
  const Read primary = read("/.crosspoint/global_stats.bin",data,size);
  if (primary == Read::Valid) return true;
  if (primary == Read::Future) return false;
  const Read backup = read("/.crosspoint/global_stats.bin.bak",data,size);
  if (backup == Read::Valid) return true;
  if (primary != Read::Missing || backup != Read::Missing) return false;
  memset(data,0,STATS_SIZE);
  data[0] = 3;
  size = STATS_SIZE;
  return true;
}

bool savePeerSnapshot(const Mac& peer, const uint8_t* data, const size_t size) {
  if (!validMac(peer) || !validStats(data,size) || !Storage.ensureDirectoryExists("/.crosspoint") ||
      !Storage.ensureDirectoryExists(DIRECTORY)) return false;
  // Bounded maintenance-only path storage. No allocation, no simultaneous file readers.
  char path[64], temporary[80], previous[80];
  snprintf(path,sizeof(path),"%s/device_%02x%02x%02x%02x%02x%02x.bin",DIRECTORY,
           peer[0],peer[1],peer[2],peer[3],peer[4],peer[5]);
  snprintf(temporary,sizeof(temporary),"%s.part",path);
  snprintf(previous,sizeof(previous),"%s.previous",path);
  if (!replaceable(path) || !replaceable(previous)) {
    LOG_ERR("NSYNC","Existing peer snapshot cannot be safely replaced");
    return false;
  }
  // Recover a rename interruption without replacing either snapshot.
  if (!Storage.exists(path) && Storage.exists(previous) && !Storage.rename(previous,path)) return false;
  if (Storage.exists(path) && matches(path,data,size)) return true;
  HalFile file;
  if (!Storage.openFileForWrite("NSYNC",temporary,file)) return false;
  const bool written = file.write(data,size) == size;
  file.flush();
  const bool closed = file.close();
  if (!written || !closed || !matches(temporary,data,size)) return false;
  if (Storage.exists(previous) && !Storage.remove(previous)) return false;
  const bool existed = Storage.exists(path);
  if (existed && !Storage.rename(path,previous)) return false;
  if (!Storage.rename(temporary,path)) {
    if (existed && !Storage.rename(previous,path)) LOG_ERR("NSYNC","Previous snapshot retained for recovery");
    return false;
  }
  // Keep the previous complete snapshot. The aggregator counts one file per device.
  return true;
}
}  // namespace nearby_stats
