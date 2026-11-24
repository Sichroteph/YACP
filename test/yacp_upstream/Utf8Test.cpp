#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include "Utf8.h"
#include "Utf8ComposeTable.h"

int main() {
  for (const auto& entry : kUtf8ComposeTable) {
    std::string input, expected;
    utf8AppendCodepoint(entry.base, input);
    utf8AppendCodepoint(entry.mark, input);
    utf8AppendCodepoint(entry.composed, expected);
    assert(utf8ComposeNfc(input) == expected);
#ifndef YACP_REFERENCE
    char buffer[32];
    std::strcpy(buffer, input.c_str());
    utf8ComposeNfcInPlace(buffer);
    assert(expected == buffer);
#endif
  }
  const std::string chinese = "\xe4\xb8\xad\xe6\x96\x87";
  assert(utf8ComposeNfc(chinese) == chinese);
  assert(utf8ComposeNfc("ASCII title") == "ASCII title");
#ifndef YACP_REFERENCE
  // CrossPoint additionally composes algorithmic Hangul L+V+T.
  std::string jamo, syllable;
  for (auto cp : {0x1100, 0x1161, 0x11a8}) utf8AppendCodepoint(cp, jamo);
  utf8AppendCodepoint(0xac01, syllable);
  assert(utf8ComposeNfc(jamo) == syllable);
#endif
  std::cout << "UTF-8: " << kUtf8ComposeTableSize << " composition pairs, CJK and ASCII preserved\n";
}
