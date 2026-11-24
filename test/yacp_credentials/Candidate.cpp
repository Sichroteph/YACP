#include <ArduinoJson.h>
#include <CredentialIntegrity.h>
#include <ObfuscationUtils.h>
#include <esp_mac.h>
#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
  assert(argc == 3);
  JsonDocument origin, candidate;
  std::ifstream source(argv[1]), wifi(argv[2]);
  assert(!deserializeJson(origin, source));
  assert(!deserializeJson(candidate, wifi));
  const std::string key = origin["target_factory_mac"] | "";
  assert(key.size() == 12);
  for (size_t i = 0; i < 6; ++i) testFactoryMac[i] = std::stoul(key.substr(2 * i, 2), nullptr, 16);
  size_t count = 0;
  for (JsonObjectConst row : candidate["credentials"].as<JsonArrayConst>()) {
    const char* encoded = row["password_obf"] | "";
    bool valid = false;
    const std::string password = obfuscation::deobfuscateFromBase64(encoded, 64, &valid, nullptr);
    assert(valid || (encoded[0] == '\0' && row["password_len"].as<size_t>() == 0));
    assert(credential_integrity::validate(password, row["password_len"].as<size_t>(), row["password_crc32"].as<uint32_t>()));
    assert(obfuscation::obfuscateToBase64(password) == encoded);
    if (!password.empty()) {
      auto changed = password;
      changed[0] ^= 1;
      assert(!credential_integrity::validate(changed, row["password_len"].as<size_t>(), row["password_crc32"].as<uint32_t>()));
    }
    ++count;
  }
  std::cout << "{\"passed\":true,\"networks\":" << count << ",\"actual_cpp_codec\":true}" << std::endl;
}
