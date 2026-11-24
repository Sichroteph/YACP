#include <cassert>
#include <fstream>
#include <iostream>

#include "CrossPointState.h"

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1]);
  assert(input.good());
  JsonDocument candidate;
  assert(!deserializeJson(candidate, input));
  const auto proposed = candidate["state"].as<JsonVariantConst>();
  assert(proposed.is<JsonObjectConst>());
  assert(APP_STATE.fromJson(proposed));
  JsonDocument stored;
  APP_STATE.toJson(stored);
  assert(proposed == stored.as<JsonVariantConst>());
  std::cout << "All proposed shared-state fields survive the actual firmware codec\n";
}
