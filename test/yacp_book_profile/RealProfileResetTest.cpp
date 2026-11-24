#include "BookReaderSettings.h"
#include <CrossPointSettings.h>
#include <HalStorage.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
  assert(argc > 1);
  constexpr char cache[] = "/.crosspoint/epub_1";
  constexpr char first[] = "/.crosspoint/book_profiles/epub_1/reader_profile.0";
  constexpr char second[] = "/.crosspoint/book_profiles/epub_1/reader_profile.1";
  for (int i = 1; i < argc; ++i) {
    std::ifstream input(argv[i], std::ios::binary);
    assert(input);
    const std::vector<uint8_t> source{std::istreambuf_iterator<char>(input), {}};
    input.close();
    assert(source.size() == BookReaderSettings::FILE_SIZE);
    testFiles.clear();
    testFiles[first] = source;
    CrossPointSettings settings;
    BookReaderSettings reader;
    reader.begin(settings, cache);
    const unsigned before = testStorageWrites;
    assert(reader.save(settings) && testStorageWrites == before);
    assert(settings.fontPointSize == source[11]);
    assert(settings.publisherPageNumbers == source[29]);
    reader.restore(settings);
    assert(settings.fontPointSize == 12);
    assert(BookReaderSettings::resetForBook(settings, cache));
    assert(testFiles[first] == source && testFiles.at(second)[9] == 0);
    assert(testStorageWrites == before + 1);
    settings.fontPointSize = 20;
    settings.autoPageTurnSeconds = 115;
    settings.guideReadingEnabled = 1;
    settings.publisherPageNumbers = 1;
    reader.begin(settings, cache);
    assert(settings.fontPointSize == 20 && settings.autoPageTurnSeconds == 115 && settings.guideReadingEnabled == 1);
    assert(settings.publisherPageNumbers == 1);
    assert(reader.save(settings) && testStorageWrites == before + 1);
  }
  printf("PASS: %d real converted profiles, load/reset/future inheritance, sources unchanged\n", argc - 1);
}
