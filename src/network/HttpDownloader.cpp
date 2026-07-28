#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <base64.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <strings.h>

#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "AppVersion.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
constexpr size_t PROGRESS_UPDATE_BYTES = 64 * 1024;
constexpr uint32_t PROGRESS_UPDATE_MS = 250;
constexpr int HTTP_RX_BUF = 4096;
constexpr int HTTP_TX_BUF = 1024;
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr int HTTP_READ_POLL_TIMEOUT_MS = 5000;
constexpr uint32_t DOWNLOAD_IDLE_TIMEOUT_MS = 30000;
constexpr size_t DEFAULT_DOWNLOAD_BUFFER_SIZE = 2048;
constexpr uint8_t MAX_REDIRECTS = 5;

void logNetworkState(const char* phase) {
  LOG_DBG("HTTP", "%s: heap free=%u maxAlloc=%u wifi=%d rssi=%d", phase, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<int>(WiFi.status()), WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
}

void logDownloadState(const char* phase, const size_t downloaded, const size_t total, const uint32_t idleMs) {
  LOG_ERR("HTTP", "%s after %zu/%zu bytes (idle=%lu ms, timeout=%lu ms)", phase, downloaded, total,
          static_cast<unsigned long>(idleMs), static_cast<unsigned long>(DOWNLOAD_IDLE_TIMEOUT_MS));
  logNetworkState(phase);
}

bool isRedirect(const int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

esp_err_t captureLocationHeader(esp_http_client_event_t* evt) {
  auto* location = static_cast<std::string*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_HEADER && location != nullptr && evt->header_key != nullptr &&
      evt->header_value != nullptr && strcasecmp(evt->header_key, "Location") == 0) {
    location->assign(evt->header_value);
  }
  return ESP_OK;
}

struct ParsedUrl {
  bool https = false;
  std::string host;
  std::string path;
  uint16_t port = 80;
};

bool parseUrl(const std::string& url, ParsedUrl& out) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return false;

  const std::string scheme = url.substr(0, schemeEnd);
  out.https = scheme == "https";
  if (!out.https && scheme != "http") return false;

  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  const std::string hostPort =
      url.substr(hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
  out.path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
  out.port = out.https ? 443 : 80;

  const size_t portSep = hostPort.rfind(':');
  if (portSep != std::string::npos) {
    out.host = hostPort.substr(0, portSep);
    const std::string portText = hostPort.substr(portSep + 1);
    if (portText.empty()) return false;
    uint32_t parsedPort = 0;
    for (const char c : portText) {
      if (c < '0' || c > '9') return false;
      parsedPort = parsedPort * 10 + static_cast<uint32_t>(c - '0');
      if (parsedPort > UINT16_MAX) return false;
    }
    if (parsedPort == 0) return false;
    out.port = static_cast<uint16_t>(parsedPort);
  } else {
    out.host = hostPort;
  }

  return !out.host.empty() && !out.path.empty();
}

bool sameOrigin(const ParsedUrl& a, const ParsedUrl& b) {
  return a.https == b.https && a.port == b.port && strcasecmp(a.host.c_str(), b.host.c_str()) == 0;
}

const char* schemeName(const ParsedUrl& url) { return url.https ? "https" : "http"; }

std::string buildRedirectUrl(const std::string& baseUrl, const std::string& location) {
  if (location.starts_with("http://") || location.starts_with("https://")) return location;

  ParsedUrl base;
  if (!parseUrl(baseUrl, base)) return location;

  std::string origin = base.https ? "https://" : "http://";
  origin += base.host;
  if ((base.https && base.port != 443) || (!base.https && base.port != 80)) {
    origin += ":";
    origin += std::to_string(base.port);
  }

  if (!location.empty() && location[0] == '/') return origin + location;

  const size_t lastSlash = base.path.rfind('/');
  const std::string parent = lastSlash == std::string::npos ? "/" : base.path.substr(0, lastSlash + 1);
  return origin + parent + location;
}

bool isCancelRequested(bool* cancelFlag, const HttpDownloader::CancelCallback& shouldCancel) {
  if (cancelFlag && *cancelFlag) return true;
  if (shouldCancel && shouldCancel()) {
    if (cancelFlag) *cancelFlag = true;
    return true;
  }
  return false;
}

class ProgressNotifier {
 public:
  ProgressNotifier(size_t total, HttpDownloader::ProgressCallback progress)
      : total_(total), progress_(std::move(progress)) {}

  void notify(size_t downloaded, bool force) {
    if (!progress_ || total_ == 0) return;

    const uint32_t now = millis();
    if (force || downloaded == total_ || downloaded - lastProgressBytes_ >= PROGRESS_UPDATE_BYTES ||
        now - lastProgressMs_ >= PROGRESS_UPDATE_MS) {
      lastProgressBytes_ = downloaded;
      lastProgressMs_ = now;
      progress_(downloaded, total_);
    }
  }

 private:
  size_t total_;
  size_t lastProgressBytes_ = 0;
  uint32_t lastProgressMs_ = 0;
  HttpDownloader::ProgressCallback progress_;
};

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  HttpDownloader::CancelCallback shouldCancel;
  size_t resumeOffset = 0;
  size_t downloaded = 0;
  size_t total = 0;
  bool rangeIgnored = false;
};

void setRequestHeaders(esp_http_client_handle_t client, const std::string& username, const std::string& password,
                       size_t resumeOffset, bool sendAuthorization) {
  esp_http_client_set_header(client, "User-Agent", "CrossInk-ESP32-" CROSSINK_VERSION);
  esp_http_client_set_header(client, "Connection", "close");
  if (resumeOffset > 0) {
    char rangeHeader[40];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%zu-", resumeOffset);
    esp_http_client_set_header(client, "Range", rangeHeader);
    LOG_DBG("HTTP", "Resuming download at byte %zu", resumeOffset);
  }
  if (sendAuthorization) {
    const std::string credentials = username + ":" + password;
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }
}

void logTlsError(esp_http_client_handle_t client, const char* phase) {
  int tlsError = 0;
  int tlsFlags = 0;
  const esp_err_t err = esp_http_client_get_and_clear_last_tls_error(client, &tlsError, &tlsFlags);
  if (err != ESP_OK || tlsError != 0 || tlsFlags != 0) {
    const int tlsCode = tlsError < 0 ? -tlsError : tlsError;
    LOG_ERR("HTTP", "%s TLS error: err=%s mbedtls=0x%x flags=0x%x", phase, esp_err_to_name(err), tlsCode, tlsFlags);
  }
}

HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     Sink& sink, const size_t bufferSize) {
  std::string currentUrl = url;

  ParsedUrl credentialOrigin;
  const bool hasCredentials = !username.empty() && !password.empty() && parseUrl(url, credentialOrigin);

  for (uint8_t hop = 0; hop < MAX_REDIRECTS; ++hop) {
    ParsedUrl currentOrigin;
    const bool currentParsed = parseUrl(currentUrl, currentOrigin);
    const bool sendAuthorization = hasCredentials && currentParsed && sameOrigin(currentOrigin, credentialOrigin);
    std::string redirectLocation;

    esp_http_client_config_t config = {};
    config.url = currentUrl.c_str();
    config.buffer_size = HTTP_RX_BUF;
    config.buffer_size_tx = HTTP_TX_BUF;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = false;
    config.event_handler = captureLocationHeader;
    config.user_data = &redirectLocation;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      LOG_ERR("HTTP", "Client init failed");
      logNetworkState("Client init failure");
      return HttpDownloader::HTTP_ERROR;
    }

    setRequestHeaders(client, username, password, sink.resumeOffset, sendAuthorization);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "Open failed: %s", esp_err_to_name(err));
      logTlsError(client, "Open failure");
      logNetworkState("Open failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    int64_t responseLength = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (responseLength < 0) {
      LOG_ERR("HTTP", "Fetch headers failed: %lld", static_cast<long long>(responseLength));
      logNetworkState("Fetch headers failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    if (isRedirect(status)) {
      if (redirectLocation.empty()) {
        LOG_ERR("HTTP", "Redirect missing Location header");
        logNetworkState("Redirect missing Location");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }

      const std::string redirectUrl = buildRedirectUrl(currentUrl, redirectLocation);
      ParsedUrl redirect;
      if (!parseUrl(redirectUrl, redirect)) {
        LOG_ERR("HTTP", "Rejected redirect with unsupported Location");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (currentParsed && currentOrigin.https && !redirect.https) {
        LOG_ERR("HTTP", "Rejected HTTPS downgrade redirect to %s", redirect.host.c_str());
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (hasCredentials && !sameOrigin(redirect, credentialOrigin)) {
        LOG_ERR("HTTP", "Rejected credentialed redirect to different origin: %s://%s:%u", schemeName(redirect),
                redirect.host.c_str(), redirect.port);
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      currentUrl = redirectUrl;
      LOG_DBG("HTTP", "Redirecting to: %s", redirect.host.c_str());
      esp_http_client_cleanup(client);
      continue;
    }

    const bool isResumeResponse = sink.resumeOffset > 0 && status == 206;
    if (status != 200 && !isResumeResponse) {
      LOG_ERR("HTTP", "Unexpected status: %d", status);
      logNetworkState("Unexpected status");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (sink.resumeOffset > 0 && !isResumeResponse) {
      LOG_DBG("HTTP", "Server ignored range request; restarting download");
      sink.rangeIgnored = true;
      sink.resumeOffset = 0;
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    const size_t bodyLength = responseLength > 0 ? static_cast<size_t>(responseLength) : 0;
    sink.total = bodyLength > 0 ? sink.resumeOffset + bodyLength : 0;
    sink.downloaded = sink.resumeOffset;
    if (sink.total > 0) {
      LOG_DBG("HTTP", "Content-Length: %zu", sink.total);
    } else {
      LOG_DBG("HTTP", "Content-Length: unknown");
    }
#ifdef ESP_ERR_HTTP_EAGAIN
    err = esp_http_client_set_timeout_ms(client, HTTP_READ_POLL_TIMEOUT_MS);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "Failed to set read timeout: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
#endif

    auto buffer = makeUniqueNoThrow<char[]>(bufferSize);
    if (!buffer) {
      LOG_ERR("HTTP", "Failed to allocate %zu byte download buffer", bufferSize);
      logNetworkState("Download buffer allocation failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    ProgressNotifier progressNotifier(sink.total, std::move(sink.progress));
    LOG_DBG("HTTP", "Reading body: buffer=%zu bytes", bufferSize);
#ifdef ESP_ERR_HTTP_EAGAIN
    uint32_t lastReadMs = millis();
#endif
    while (true) {
      if (isCancelRequested(sink.cancelFlag, sink.shouldCancel)) {
        esp_http_client_cleanup(client);
        return HttpDownloader::ABORTED;
      }

      const int bytesRead = esp_http_client_read(client, buffer.get(), bufferSize);
      if (bytesRead < 0) {
#ifdef ESP_ERR_HTTP_EAGAIN
        if (bytesRead == -ESP_ERR_HTTP_EAGAIN) {
          const uint32_t idleMs = millis() - lastReadMs;
          if (idleMs >= DOWNLOAD_IDLE_TIMEOUT_MS) {
            logDownloadState("Read timed out", sink.downloaded, sink.total, idleMs);
            esp_http_client_cleanup(client);
            return HttpDownloader::HTTP_ERROR;
          }
          delay(1);
          continue;
        }
#endif
        LOG_ERR("HTTP", "Read error after %zu/%zu bytes", sink.downloaded, sink.total);
        logNetworkState("Read error");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (bytesRead == 0) break;

      if (!sink.write(reinterpret_cast<const uint8_t*>(buffer.get()), static_cast<size_t>(bytesRead))) {
        LOG_ERR("HTTP", "Write failed after %zu/%zu bytes", sink.downloaded, sink.total);
        logNetworkState("Write failure");
        esp_http_client_cleanup(client);
        return HttpDownloader::FILE_ERROR;
      }

      sink.downloaded += static_cast<size_t>(bytesRead);
#ifdef ESP_ERR_HTTP_EAGAIN
      lastReadMs = millis();
#endif
      if (sink.total > 0 && sink.total <= PROGRESS_UPDATE_BYTES) {
        LOG_DBG("HTTP", "Read progress: %zu/%zu bytes", sink.downloaded, sink.total);
      }
      progressNotifier.notify(sink.downloaded, false);
      if (sink.total > 0 && sink.downloaded >= sink.total) break;
      delay(0);
    }

    const bool complete = esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    progressNotifier.notify(sink.downloaded, true);
    if (!complete) {
      LOG_ERR("HTTP", "Incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      logNetworkState("Incomplete transfer");
      return HttpDownloader::HTTP_ERROR;
    }

    return HttpDownloader::OK;
  }

  LOG_ERR("HTTP", "Redirect limit exceeded");
  logNetworkState("Redirect limit exceeded");
  return HttpDownloader::HTTP_ERROR;
}
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password);
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  outContent.clear();
  return fetchUrl(
      url,
      [&outContent](const uint8_t* data, size_t len) {
        outContent.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      username, password);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  WifiPowerSaveGuard wifiPowerSaveGuard;
  (void)wifiPowerSaveGuard;

  LOG_DBG("HTTP", "Fetching: %s", url.c_str());

  if (!onData) {
    LOG_ERR("HTTP", "Fetch failed: missing data callback");
    return false;
  }

  Sink sink;
  sink.write = onData;
  return runGet(url, username, password, sink, DEFAULT_DOWNLOAD_BUFFER_SIZE) == OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             DownloadOptions options) {
  WifiPowerSaveGuard wifiPowerSaveGuard;
  (void)wifiPowerSaveGuard;

  const size_t bufferSize = options.bufferSize > 0 ? options.bufferSize : DEFAULT_DOWNLOAD_BUFFER_SIZE;
  size_t resumeOffset = 0;
  if (options.resumePartial && Storage.exists(destPath.c_str())) {
    FsFile existingFile;
    if (Storage.openFileForRead("HTTP", destPath.c_str(), existingFile)) {
      resumeOffset = existingFile.fileSize();
      existingFile.close();
    }
  }

  LOG_DBG("HTTP", "Downloading: %s", url.c_str());
  LOG_DBG("HTTP", "Destination: %s", destPath.c_str());
  LOG_DBG("HTTP", "Timeout: %d ms buffer=%zu bytes", HTTP_TIMEOUT_MS, bufferSize);

  if (resumeOffset == 0 && Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.shouldCancel = std::move(options.shouldCancel);
  sink.resumeOffset = resumeOffset;

  FsFile file;
  bool fileOpen = false;
  auto openOutputFile = [&]() {
    if (fileOpen) return true;
    if (sink.resumeOffset > 0) {
      file = Storage.open(destPath.c_str(), O_WRONLY | O_APPEND);
    } else {
      fileOpen = Storage.openFileForWrite("HTTP", destPath.c_str(), file);
      if (!fileOpen) {
        LOG_ERR("HTTP", "Failed to open file for writing");
        return false;
      }
    }
    fileOpen = file;
    if (!fileOpen) {
      LOG_ERR("HTTP", "Failed to open file for writing");
    }
    return fileOpen;
  };

  sink.write = [&](const uint8_t* data, size_t len) { return openOutputFile() && file.write(data, len) == len; };

  DownloadError result = runGet(url, username, password, sink, bufferSize);
  if (sink.rangeIgnored) {
    if (fileOpen) {
      file.close();
      fileOpen = false;
    }
    Storage.remove(destPath.c_str());
    sink.rangeIgnored = false;
    sink.resumeOffset = 0;
    sink.downloaded = 0;
    sink.total = 0;
    sink.write = [&](const uint8_t* data, size_t len) { return openOutputFile() && file.write(data, len) == len; };
    result = runGet(url, username, password, sink, bufferSize);
  }

  if (fileOpen) {
    file.flush();
    file.close();
  }

  if (result != OK) {
    LOG_ERR("HTTP", "Transfer failed: error=%d downloaded=%zu expected=%zu preservePartial=%d resumePartial=%d",
            static_cast<int>(result), sink.downloaded, sink.total, options.preservePartial, options.resumePartial);
    if (result == ABORTED || !options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return result;
  }

  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    if (!options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return HTTP_ERROR;
  }

  if (sink.total > 0 && sink.downloaded != sink.total) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", sink.downloaded, sink.total);
    if (!options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return HTTP_ERROR;
  }

  LOG_DBG("HTTP", "Downloaded %zu bytes", sink.downloaded);
  return OK;
}
