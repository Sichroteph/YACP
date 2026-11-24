#include "OtaUpdater.h"

#include "HttpDownloader.h"
#include <BoardConfig.h>
#include <Logging.h>
#include <Memory.h>
#include <ReleaseJsonParser.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstring>

#include "FirmwareFlasher.h"
#include "YacpReleasePolicy.h"

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/Sichroteph/YACP/releases/latest";
constexpr char stagePath[] = "/.crosspoint/yacp-ota-update.bin";
constexpr char assetPrefix[] = "https://github.com/Sichroteph/YACP/releases/download/";

const char* variant() {
#if FREEINK_DEVICE_X4PRO
#ifdef YACP_XLARGE
  return "x4pro-xlarge";
#else
  return "x4pro";
#endif
#elif FREEINK_DEVICE_X4CLASSIC
#ifdef YACP_XLARGE
  return "x4c-xlarge";
#else
  return "x4c";
#endif
#elif FREEINK_DEVICE_X3 || FREEINK_DEVICE_X4
#ifdef YACP_XLARGE
  return "xlarge";
#else
  return "tiny";
#endif
#else
  return nullptr;
#endif
}

bool matchesAsset(const char* name) {
  const char* selected = variant();
  return selected && yacp_release::matchesVariant(name, selected);
}

// After downloading, release the TLS session before allocating this buffer.
// Maintenance only: one fallible 1 KiB allocation instead of a large stack frame.
OtaUpdater::OtaUpdaterError verifyStaged(size_t expectedSize, const uint8_t (&expected)[32], const char* tag) {
  auto buffer = makeUniqueNoThrow<uint8_t[]>(1024);
  if (!buffer) {
    LOG_ERR("OTA", "No memory for SD verification");
    return OtaUpdater::OOM_ERROR;
  }
  HalFile file;
  if (!Storage.openFileForRead("OTA", stagePath, file)) return OtaUpdater::INTERNAL_UPDATE_ERROR;
  ScopedCleanup closeFile{[&file] { file.close(); }};
  if (file.size() != expectedSize) {
    LOG_ERR("OTA", "Downloaded size differs from release");
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  ScopedCleanup freeHash{[&sha] { mbedtls_sha256_free(&sha); }};
  if (mbedtls_sha256_starts(&sha, 0) != 0) {
    LOG_ERR("OTA", "Cannot initialize SHA-256");
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }
  size_t remaining = expectedSize;
  while (remaining) {
    const size_t count = std::min(remaining, size_t{1024});
    if (file.read(buffer.get(), count) != static_cast<int>(count) ||
        mbedtls_sha256_update(&sha, buffer.get(), count) != 0) {
      LOG_ERR("OTA", "Cannot hash staged file");
      return OtaUpdater::INTERNAL_UPDATE_ERROR;
    }
    remaining -= count;
  }
  uint8_t actual[32];
  if (mbedtls_sha256_finish(&sha, actual) != 0 || std::memcmp(actual, expected, sizeof(actual))) {
    LOG_ERR("OTA", "Release SHA-256 mismatch");
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  // ESP image header + first segment header precede the application descriptor.
  // Require the embedded YACP version/variant as well as the downloaded filename.
  char embedded[32] = {}, wanted[48] = {};
  const char* suffix = variant();
  if (!std::strcmp(suffix, "x4pro")) suffix = "x4pro-tiny";
  if (!std::strcmp(suffix, "x4c")) suffix = "x4c-tiny";
  if (*tag == 'v') ++tag;
  const int length = std::snprintf(wanted, sizeof(wanted), "%s-%s", tag, suffix);
  if (length <= 0 || length >= static_cast<int>(sizeof(embedded)) ||
      !file.seek(24 + 8 + offsetof(esp_app_desc_t, version)) ||
      file.read(embedded, sizeof(embedded)) != sizeof(embedded) ||
      std::memcmp(embedded, wanted, length + 1)) {
    LOG_ERR("OTA", "Embedded version/variant differs from release");
    return OtaUpdater::WRONG_DEVICE_ERROR;
  }
  return OtaUpdater::OK;
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = totalSize = processedSize = 0;
  std::memset(expectedSha256, 0, sizeof(expectedSha256));
  if (!variant()) return NO_UPDATE;
  LOG_DBG("OTA", "Checking YACP update (current: %s)", CROSSPOINT_VERSION);

  // The fixed parser exceeds 2 KiB, so do not put it on the activity stack.
  // Stream the response and free this one fallible allocation before downloading.
  auto parser = makeUniqueNoThrow<ReleaseJsonParser>();
  if (!parser) {
    LOG_ERR("OTA", "No memory for release parser");
    return OOM_ERROR;
  }
  parser->setFirmwareAssetMatcher(matchesAsset);
  if (!HttpDownloader::fetchVerifiedMetadata(latestReleaseUrl, [&](const uint8_t* data, size_t size) {
        parser->feed(reinterpret_cast<const char*>(data), size);
        return true;
      })) {
    LOG_ERR("OTA", "Verified release check failed");
    return HTTP_ERROR;
  }
  yacp_release::Version version;
  if (!parser->complete() || !parser->foundTag() || !yacp_release::parseVersion(parser->getTagName(), version)) {
    LOG_ERR("OTA", "Invalid or incomplete YACP release metadata");
    return JSON_PARSE_ERROR;
  }
  if (!parser->foundFirmware()) return NO_UPDATE;
  char expectedName[64];
  if (!yacp_release::assetName(expectedName, sizeof(expectedName), parser->getTagName(), variant()) ||
      std::strcmp(expectedName, parser->getSelectedAssetName()) ||
      !yacp_release::digest(parser->getFirmwareDigest(), expectedSha256) ||
      parser->getFirmwareSize() < 64 * 1024 || parser->getFirmwareSize() > 16 * 1024 * 1024 ||
      std::strncmp(parser->getFirmwareUrl(), assetPrefix, sizeof(assetPrefix) - 1)) {
    LOG_ERR("OTA", "Missing or inconsistent YACP asset metadata");
    return JSON_PARSE_ERROR;
  }
  latestVersion = parser->getTagName();
  otaUrl = parser->getFirmwareUrl();
  otaSize = parser->getFirmwareSize();
  totalSize = otaSize * 2;
  updateAvailable = true;
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  return updateAvailable && yacp_release::newer(latestVersion.c_str(), CROSSPOINT_VERSION);
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) return UPDATE_OLDER_ERROR;
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition || otaSize > partition->size) {
    LOG_ERR("OTA", "YACP asset does not fit the inactive OTA partition");
    return INTERNAL_UPDATE_ERROR;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return INTERNAL_UPDATE_ERROR;
  processedSize = 0;
  int lastPercent = -1;
  auto report = [&] {
    const int percent = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
    if (onProgress && percent != lastPercent) {
      lastPercent = percent;
      onProgress(ctx);
    }
  };
  const auto download = HttpDownloader::downloadToFile(otaUrl, stagePath, [&](size_t count, size_t) {
    processedSize = std::min(count, otaSize);
    report();
  });
  if (download != HttpDownloader::OK) {
    LOG_ERR("OTA", "YACP download failed: %d", download);
    return download == HttpDownloader::FILE_ERROR ? INTERNAL_UPDATE_ERROR : HTTP_ERROR;
  }
  const auto verified = verifyStaged(otaSize, expectedSha256, latestVersion.c_str());
  if (verified != OK) return verified;
  // CrossPoint validates the complete image, chip and board BEFORE any erase.
  // Never bypass that validation, even after the independent release hash check.
  struct Progress {
    OtaUpdater* updater;
    ProgressCallback callback;
    void* context;
    int lastPercent;
  } progress{this, onProgress, ctx, lastPercent};
  const auto result = firmware_flash::flashFromSdPath(stagePath, [](size_t written, size_t, void* context) {
    auto& p = *static_cast<Progress*>(context);
    p.updater->processedSize = p.updater->otaSize + written;
    const int percent = static_cast<int>(static_cast<uint64_t>(p.updater->processedSize) * 100 / p.updater->totalSize);
    if (p.callback && percent != p.lastPercent) {
      p.lastPercent = percent;
      p.callback(p.context);
    }
  }, &progress);
  if (result != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "YACP install failed: %s", firmware_flash::resultName(result));
    if (result == firmware_flash::Result::BAD_CHIP || result == firmware_flash::Result::WRONG_BOARD) return WRONG_DEVICE_ERROR;
    return result == firmware_flash::Result::OOM ? OOM_ERROR : INTERNAL_UPDATE_ERROR;
  }
  LOG_INF("OTA", "YACP image verified and installed");
  return OK;
}
