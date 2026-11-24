#pragma once
#include "Epub.h"
class Xtc : public Epub {
 public:
  using Epub::Epub;
  bool load() { return false; }
};
