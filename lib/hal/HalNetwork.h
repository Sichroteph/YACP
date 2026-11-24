#pragma once

class HalNetwork {
 public:
  bool enableTcpDontFragmentOnStation();
};

extern HalNetwork halNetwork;
