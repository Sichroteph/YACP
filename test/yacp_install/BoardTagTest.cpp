#include <algorithm>
#include <cassert>
#include <string>

#include "FirmwareBoardTag.h"

int main() {
  for (const std::string image : {"header-CROSSPOINT-BOARD-V1:x4;-end", "untagged-firmware"}) {
    for (size_t chunk = 1; chunk <= image.size(); ++chunk) {
      board_tag::Scanner scanner;
      for (size_t at = 0; at < image.size(); at += chunk)
        scanner.feed(reinterpret_cast<const uint8_t*>(image.data() + at), std::min(chunk, image.size() - at));
      assert(!scanner.mismatch());
    }
  }
  const std::string wrong = "CROSSPOINT-BOARD-V1:x4pro;";
  for (size_t chunk = 1; chunk <= wrong.size(); ++chunk) {
    board_tag::Scanner scanner;
    for (size_t at = 0; at < wrong.size(); at += chunk)
      scanner.feed(reinterpret_cast<const uint8_t*>(wrong.data() + at), std::min(chunk, wrong.size() - at));
    assert(scanner.mismatch());
  }
}
