#include <ReferencePageParser.h>
#include <cmath>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const auto text = std::string(std::istreambuf_iterator<char>(std::cin), {});
  const size_t chunk = std::stoul(argv[1]);
  ReferencePageParser parser(static_cast<uint16_t>(std::stoul(argv[2])));
  for (size_t i = 0; i < text.size(); i += chunk) parser.feed(text.data() + i, std::min(chunk, text.size() - i));
  ReferencePageInfo info;
  if (!parser.finish(info)) { std::cout << "null\n"; return 0; }
  uint32_t page, count;
  if (info.resolve(NAN, page, count) || page || count) return 3;
  std::cout << '[' << info.start << ',' << info.count << ',' << info.total << ',' << info.unitsPerPage << ',' << info.pages;
  for (float fraction : {-1.f, 0.f, .25f, .5f, .75f, 1.f, 2.f}) {
    if (!info.resolve(fraction, page, count) || count != info.pages) return 4;
    std::cout << ',' << page;
  }
  std::cout << "]\n";
}
