#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "Bitmap.h"

int main(int argc,char** argv) {
  assert(argc==3);
  std::ifstream input(argv[1],std::ios::binary); assert(input);
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
  HalFile file(bytes);
  const bool adaptive=std::string(argv[2])=="adaptive";
  const bool native=std::string(argv[2])!="legacy";
#ifdef YACP_REFERENCE
  Bitmap bitmap(file,true,adaptive ? BitmapToneMapping::Adaptive : native ? BitmapToneMapping::Native : BitmapToneMapping::Legacy);
#else
  Bitmap bitmap(file,true,native);
#endif
  if(bitmap.parseHeaders()!=BmpReaderError::Ok) return 2;
#if defined(YACP_REFERENCE) || defined(YACP_ADAPTIVE_TEST)
  if(adaptive && !bitmap.analyzeAdaptiveToneMapping()) return 3;
#else
  if(adaptive) return 4;
#endif
  std::vector<uint8_t> row(bitmap.getRowBytes());
  std::vector<uint8_t> pixels((bitmap.getWidth()+3)/4);
  std::vector<uint8_t> first;
  for(unsigned pass=0;pass<2;++pass) {
    assert(bitmap.rewindToData()==BmpReaderError::Ok);
    std::vector<uint8_t> decoded;
    for(int y=0;y<bitmap.getHeight();++y) {
      if(bitmap.readNextRow(pixels.data(),row.data())!=BmpReaderError::Ok) return 5;
      decoded.insert(decoded.end(),pixels.begin(),pixels.end());
    }
    if(!pass) first=decoded;
    else assert(first==decoded);
  }
  for(uint8_t b:first) std::printf("%02x",b);
  std::puts("");
}
