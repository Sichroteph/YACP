#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "YacpReleasePolicy.h"
#include "ReleaseJsonParser.h"

bool tiny(const char* name) { return yacp_release::matchesVariant(name, "tiny"); }
int main() {
  using namespace yacp_release;
  Version v;
  for (const char* s : {"", "1", "1.2", "1.2.3", "1.2.3-yac", "1.2.3-yacp.",
                        "1.2.3-yacp.-1", "4294967296.1.2-yacp", "1.2.3-yacp.4294967296",
                        "v1.2.3-yacp-tiny", "1.2.3-yacp junk"}) assert(!parseVersion(s, v));
  assert(parseVersion("v1.7.2-yacp", v));
  assert(parseVersion("1.7.2-yacp.168-x4pro-tiny", v, true));
  assert(!newer("v1.7.1-yacp", "1.7.2-yacp.168-tiny"));
  assert(newer("v1.7.2-yacp", "1.7.2-yacp.168-tiny"));
  assert(!newer("v1.7.2-yacp.169", "1.7.2-yacp-tiny"));
  assert(newer("1.7.2-yacp.169", "1.7.2-yacp.168-xlarge"));
  assert(!newer("v1.7.2-yacp", "1.7.2-yacp-x4c-tiny"));
  char name[64];
  for (const char* variant : {"tiny", "xlarge", "x4pro", "x4pro-xlarge", "x4c", "x4c-xlarge"}) {
    assert(assetName(name, sizeof(name), "v1.7.2-yacp", variant));
    assert(matchesVariant(name, variant));
    for (const char* other : {"tiny", "xlarge", "x4pro", "x4pro-xlarge", "x4c", "x4c-xlarge"})
      if (std::strcmp(variant, other)) assert(!matchesVariant(name, other));
  }
  assert(!matchesVariant("CrossPoint-1.7.2-yacp-tiny.bin", "tiny"));
  assert(!matchesVariant("YACP-1.7.2-yacp-x4pro-tiny.bin", "tiny"));
  assert(!matchesVariant("YACP-1.7.2-yacp-tiny.bin.exe", "tiny"));
  uint8_t hash[32];
  const std::string sha = "sha256:" + std::string(64, 'a');
  assert(digest(sha.c_str(), hash) && hash[0] == 0xaa);
  assert(!digest((sha + "0").c_str(), hash));
  assert(!digest(("sha256:" + std::string(64, 'g')).c_str(), hash));
  const std::string asset = "{\"name\":\"YACP-1.7.2-yacp-tiny.bin\",\"size\":123456,"
      "\"browser_download_url\":\"https://example.test/image\",\"digest\":\"" + sha + "\"}";
  // GitHub object key ordering is not a contract. Include nested irrelevant data.
  const std::string json = "{\"assets\":[{\"name\":\"unrelated.bin\",\"uploader\":{\"name\":\"ignored\"}}," +
      asset + "],\"body\":\"" + std::string(1500, 'x') + "\",\"tag_name\":\"v1.7.2-yacp\"}";
  for (unsigned chunk : {1, 2, 7, 64, 511, 2000}) {
    ReleaseJsonParser parser;
    parser.setFirmwareAssetMatcher(tiny);
    for (size_t i = 0; i < json.size(); i += chunk) parser.feed(json.data() + i, std::min<size_t>(chunk, json.size()-i));
    assert(parser.complete() && parser.foundTag() && parser.foundFirmware());
    assert(parser.getFirmwareSize() == 123456);
    assert(std::strcmp(parser.getFirmwareDigest(), sha.c_str()) == 0);
    parser.reset();
    parser.feed(json.data(), json.size()-1);
    assert(!parser.complete());
  }
  for (const auto& broken : std::initializer_list<std::string>{"{\"assets\":[" + asset + "," + asset + "]}",
                             "{\"assets\":[{\"size\":-1}]}", "{\"assets\":[{\"size\":1e9}]}",
                             "{\"tag_name\":\"a\",\"tag_name\":\"b\"}",
                             "{\"assets\":[{\"size\":999999999999999999999999999999}]}", "{}{}"}) {
    ReleaseJsonParser parser;
    parser.setFirmwareAssetMatcher(tiny);
    parser.feed(broken.data(), broken.size());
    assert(!parser.complete());
  }
  // Retain the existing upstream exact-name parser API.
  ReleaseJsonParser upstream;
  const std::string old = R"({"tag_name":"1.6.5","assets":[{"name":"firmware.bin","size":123,"browser_download_url":"https://example.test"}]})";
  upstream.feed(old.data(), old.size());
  assert(upstream.foundFirmware() && upstream.complete());
  std::puts("YACP release policy and streaming parser: passed");
}
