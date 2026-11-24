#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include "HalStorage.h"
#include "StatsBackup.h"
#include "ReadingStatsUtils.h"

static bool hasDate = true;
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime& result) {
  result = {{2026, 10, 1}, 12, 30, 0};
  return hasDate;
}
static const std::string source = "/.crosspoint/global_stats.bin";
static const std::string directory = "/.crossink-stats-backup/";
static const std::string daily = directory + "stats_2026-10-01.bin";

void reset() {
  testFiles.clear(); testStorageWrites = 0; failStatsWrite = false; failStatsClose = false;
  failStatsRenameTo.clear(); hasDate = true;
  testFiles[source] = std::vector<uint8_t>(159, 42); testFiles[source][0] = 3;
}

int main(int argc, char** argv) {
  if (argc == 2) {
    reset();
    std::ifstream file(argv[1], std::ios::binary);
    assert(file);
    testFiles[source] = std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    const auto original = testFiles.at(source);
    assert(backupGlobalStats(false));
    assert(testFiles.at(daily) == original && testFiles.at(source) == original);
    std::puts("Real statistics copy: exact in-memory backup verified; host source read only");
    return 0;
  }
  reset();
  char name[64];
  assert(backupGlobalStats(false, name, sizeof(name)));
  assert(testFiles.at(daily) == testFiles.at(source));
  assert(std::string(name) == "stats_2026-10-01.bin");
  const unsigned writes = testStorageWrites;
  assert(backupGlobalStats(false) && testStorageWrites == writes);
  assert(backupGlobalStats(true, name, sizeof(name)));
  assert(std::string(name) == "stats_2026-10-01_1230.bin");
  testFiles[source][1] = 77;
  assert(backupGlobalStats(false) && testFiles.at(daily) == testFiles.at(source));
  assert(!Storage.exists(daily + ".previous"));

  for (int failure = 0; failure < 3; ++failure) {
    reset(); assert(backupGlobalStats(false));
    const auto old = testFiles.at(daily);
    testFiles[source][1] = 23;
    failStatsWrite = failure == 0; failStatsClose = failure == 1;
    if (failure == 2) failStatsRenameTo = daily;
    assert(!backupGlobalStats(false));
    assert((Storage.exists(daily) && testFiles.at(daily) == old) || testFiles.at(daily + ".previous") == old);
    failStatsWrite = failStatsClose = false; failStatsRenameTo.clear();
    assert(backupGlobalStats(false));
    assert(testFiles.at(daily) == testFiles.at(source));
  }

  reset();
  testFiles[daily + ".previous"] = testFiles.at(source);
  testFiles[source][5] = 12;
  assert(backupGlobalStats(false));
  assert(testFiles.at(daily) == testFiles.at(source));
  assert(!Storage.exists(daily + ".previous"));

  reset(); hasDate = false;
  testFiles[directory + "stats_notes.bin"] = {1,2,3};
  for (unsigned i = 1; i <= 10; ++i) assert(backupGlobalStats(true, name, sizeof(name)));
  assert(std::string(name) == "stats_backup_010.bin");
  assert(!Storage.exists(directory + "stats_backup_003.bin"));
  assert(Storage.exists(directory + "stats_backup_004.bin"));
  assert(Storage.exists(directory + "stats_notes.bin"));
  assert(testFiles.size() == 9); // Source, seven archives, unrelated file.

  reset(); hasDate = false;
  for (unsigned i = 994; i <= 1000; ++i) {
    snprintf(name, sizeof(name), "stats_backup_%03u.bin", i);
    testFiles[directory + name] = testFiles.at(source);
  }
  assert(backupGlobalStats(true));
  assert(!Storage.exists(directory + "stats_backup_994.bin"));
  assert(Storage.exists(directory + "stats_backup_1000.bin"));

  for (unsigned version = 1; version <= 2; ++version) {
    reset(); testFiles[source].assign(version == 1 ? 13 : 17, 0); testFiles[source][0] = version;
    assert(backupGlobalStats(false) && testFiles.at(daily) == testFiles.at(source));
  }
  for (unsigned version : {0, 4, 255}) {
    reset(); testFiles[source][0] = version;
    const auto before = testFiles;
    assert(!backupGlobalStats(false) && testFiles == before);
  }
  reset(); testFiles[source].resize(158);
  assert(!backupGlobalStats(false));
  reset(); hasDate = false;
  testFiles[directory + "stats_backup_4294967295.bin"] = testFiles.at(source);
  assert(!backupGlobalStats(true));
  std::puts("Statistics backup: exact versions, idempotence, rotation, interruption, I/O failures and bounded retention passed");
}
