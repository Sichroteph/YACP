#include <cassert>
#include <cstdio>
#include "OpdsServerStore.h"

int main() {
  JsonDocument source;
  assert(!deserializeJson(source, R"({"servers":[
    {"name":"first","url":"https://example.invalid/a","username":"qa","password_obf":"synthetic","filenameFormat":0},
    {"name":"second","url":"https://example.invalid/b","filenameFormat":1},
    {"name":"inherit","url":"https://example.invalid/c"}]})"));
  auto& store = OPDS_STORE;
  assert(store.fromJson(source.as<JsonVariantConst>()));
  assert(store.getCount() == 3);
  for (int global = 0; global < 3; ++global) {
    assert(store.getServer(0)->effectiveFilenameFormat(global) == 0);
    assert(store.getServer(1)->effectiveFilenameFormat(global) == 1);
    assert(store.getServer(2)->effectiveFilenameFormat(global) == global);
  }
  JsonDocument encoded;
  store.toJson(encoded);
  assert(encoded["servers"][2]["filenameFormat"] == 255);
  assert(store.fromJson(encoded.as<JsonVariantConst>()));
  assert(store.getServer(0)->password == "synthetic");
  auto changed = *store.getServer(0);
  changed.name = "changed";
  changed.filenameFormat = 2;
  persistOk = false;
  assert(!store.updateServer(0, changed));
  assert(store.getServer(0)->name == "first" && store.getServer(0)->filenameFormat == 0);
  assert(!store.addServer(changed) && store.getCount() == 3);
  persistOk = true;
  assert(store.updateServer(0, changed));
  assert(store.updateServer(0, *store.getServer(0)));  // Aliasing must not move the input away.
  assert(store.getServer(0)->name == "changed" && store.getServer(0)->filenameFormat == 2);
  assert(saveCalls == 4);
  std::puts("OPDS store passed: overrides, inheritance, serialization, failed saves and aliasing");
}
