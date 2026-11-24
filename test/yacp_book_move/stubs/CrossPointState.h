#pragma once
#include "HalStorage.h"
struct FakeState {
  std::string openEpubPath;
  bool saveToFile() {
    auto& data = files["/.crosspoint/state.json"];
    if (fail()) { data = {'{'}; return false; }
    const auto json = "state:" + openEpubPath;
    data.assign(json.begin(), json.end());
    return true;
  }
};
inline FakeState APP_STATE;
