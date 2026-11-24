#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "GlobalReadingStats.h"
#include "HalStorage.h"

int main(int argc, char** argv) {
  assert(argc == 3);
  std::ifstream file(argv[1], std::ios::binary);
  assert(file);
  const std::vector<uint8_t> original{std::istreambuf_iterator<char>(file), {}};
  assert(!original.empty());
  assert((original[0] == 1 && original.size() == 13) || (original[0] == 2 && original.size() == 17) ||
         (original[0] == 3 && original.size() == 159));
  const std::string primary = "/.crosspoint/global_stats.bin";
  const std::string backup = primary + ".bak";
  const std::string mode = argv[2];
  testFiles[backup] = original;
  if (mode == "direct") testFiles[primary] = original;
  else if (mode == "corrupt") testFiles[primary] = {0, 1, 2};
  else if (mode == "truncated") testFiles[primary] = std::vector<uint8_t>(original.begin(), original.end() - 1);
  else if (mode == "future-primary") { testFiles[primary] = original; testFiles[primary][0] = 4; }
  else if (mode == "future-backup") testFiles[backup][0] = 4;
  else assert(mode == "missing");
  const auto before = testFiles;
  const auto stats = GlobalReadingStats::load();
  assert(testFiles == before && testStorageWrites == 0);
  stats.save();
  if (mode.rfind("future-", 0) == 0) {
    assert(testFiles == before && testStorageWrites == 0);
  } else {
    auto expected = original;
    expected.resize(159, 0);
    expected[0] = 3;
    assert(testFiles.at(primary) == expected);
    const auto writes = testStorageWrites;
    const auto reloaded = GlobalReadingStats::load();
    assert(testStorageWrites == writes);
    reloaded.save();
    assert(testFiles.at(primary) == expected);
  }
  std::puts("PASS");
}
