#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace DictionaryHistory {

constexpr int MAX_WORDS = 128;

struct WordList {
  std::unique_ptr<char[]> text;
  std::array<uint16_t, MAX_WORDS> offsets{};
  int count = 0;
};

// Newest first. Reads and writes use one temporary bounded buffer.
void record(const char* dictionary, const std::string& word);
bool read(const char* dictionary, int index, std::string& word, int& count);
bool readList(const char* dictionary, WordList& words);
bool remove(const char* dictionary, int index);
bool restore(const char* dictionary, int index, const std::string& word);

}  // namespace DictionaryHistory
