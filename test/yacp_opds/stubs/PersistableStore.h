#pragma once
#include <ArduinoJson.h>
#include <string>

inline bool persistOk = true;
inline int saveCalls = 0;
template<class T> class PersistableStore {
 public:
  static T& getInstance() { static T instance; return instance; }
  bool saveToFile() { ++saveCalls; return persistOk; }
  void requestResave() {}
  static std::string extractPassword(JsonVariantConst value, bool&) {
    return value["password_obf"] | std::string{};
  }
};
