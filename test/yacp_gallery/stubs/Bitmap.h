#pragma once
#include "HalStorage.h"
enum class BmpReaderError { Ok, Invalid };
class Bitmap {
  HalFile& file;
 public:
  explicit Bitmap(HalFile& f):file(f) {}
  BmpReaderError parseHeaders() { char bytes[2]; return file.read(bytes,2)==2 && bytes[0]=='B' && bytes[1]=='M' ? BmpReaderError::Ok : BmpReaderError::Invalid; }
};
