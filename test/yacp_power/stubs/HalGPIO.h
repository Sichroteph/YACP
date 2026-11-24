#pragma once
struct TestGPIO {
  bool x3 = true;
  bool deviceIsX3() const { return x3; }
};
inline TestGPIO gpio;
