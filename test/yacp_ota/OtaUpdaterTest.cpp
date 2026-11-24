#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <openssl/sha.h>
#include <esp_ota_ops.h>
#include "HttpDownloader.h"
#include "OtaUpdater.h"
#include "FirmwareFlasher.h"

static std::string response;
static std::vector<uint8_t> downloadImage;
static bool metadataWorks = true, downloadWorks = true, partitionAvailable = true;
static size_t partitionSize = 0x640000;
static unsigned flashCalls = 0, downloadCalls = 0;
static firmware_flash::Result flashResult = firmware_flash::Result::OK;
const esp_partition_t* esp_ota_get_next_update_partition(const void*) {
  static esp_partition_t partition; partition.size = partitionSize;
  return partitionAvailable ? &partition : nullptr;
}
bool HttpDownloader::fetchVerifiedMetadata(const std::string& url, const DataCallback& callback) {
  assert(url == "https://api.github.com/repos/Sichroteph/YACP/releases/latest");
  if (!metadataWorks) return false;
  for (size_t i = 0; i < response.size(); i += 7)
    if (!callback(reinterpret_cast<const uint8_t*>(response.data()+i), std::min<size_t>(7, response.size()-i))) return false;
  return true;
}
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string&, const std::string& path,
    ProgressCallback progress, bool*, const std::string&, const std::string&, bool downgrade) {
  assert(path == "/.crosspoint/yacp-ota-update.bin" && !downgrade);
  ++downloadCalls;
  if (!downloadWorks) return HTTP_ERROR;
  stagedImage = downloadImage; stageExists = true;
  if (progress) progress(stagedImage.size(), stagedImage.size());
  return OK;
}
namespace firmware_flash {
Result flashFromSdPath(const char* path, ProgressCb callback, void* context, bool alreadyValidated) {
  assert(std::strcmp(path,"/.crosspoint/yacp-ota-update.bin") == 0);
  assert(!alreadyValidated && openHandles == 0);
  ++flashCalls;
  if (callback) callback(stagedImage.size(), stagedImage.size(), context);
  return flashResult;
}
const char* resultName(Result) { return "stubbed result"; }
}
static std::string hash(const std::vector<uint8_t>& image) {
  unsigned char digest[32]; SHA256(image.data(),image.size(),digest);
  char out[65]; for (unsigned i=0;i<32;++i) std::sprintf(out+i*2,"%02x",digest[i]);
  return "sha256:" + std::string(out);
}
static void setup(const char* embedded = "1.7.3-yacp-tiny", const char* asset = "YACP-1.7.3-yacp-tiny.bin") {
  downloadImage.assign(70000,0);
  std::memcpy(downloadImage.data()+48, embedded, std::strlen(embedded)+1);
  response = "{\"tag_name\":\"v1.7.3-yacp\",\"assets\":[{\"name\":\"" + std::string(asset) +
    "\",\"size\":70000,\"digest\":\"" + hash(downloadImage) +
    "\",\"browser_download_url\":\"https://github.com/Sichroteph/YACP/releases/download/v1.7.3-yacp/" + asset + "\"}]}";
  metadataWorks=downloadWorks=partitionAvailable=true; failStageRead=false; partitionSize=0x640000;
  flashCalls=downloadCalls=0; flashResult=firmware_flash::Result::OK;
}
static void rejected(OtaUpdater::OtaUpdaterError error) {
  OtaUpdater updater; assert(updater.checkForUpdate()==OtaUpdater::OK);
  assert(updater.installUpdate()==error && flashCalls==0 && openHandles==0);
}
int main(int argc, char** argv) {
  if (argc==2) {
    std::ifstream file(argv[1]); assert(file);
    response.assign(std::istreambuf_iterator<char>(file),{});
    OtaUpdater updater;
    assert(updater.checkForUpdate()==OtaUpdater::OK);
    assert(!updater.isUpdateNewer());
    assert(updater.installUpdate()==OtaUpdater::UPDATE_OLDER_ERROR);
    assert(downloadCalls==0 && flashCalls==0);
    std::puts("Real release metadata parsed; older release cannot install"); return 0;
  }
  setup(); OtaUpdater updater; assert(updater.checkForUpdate()==OtaUpdater::OK && updater.isUpdateNewer());
  unsigned callbacks=0;
  assert(updater.installUpdate([](void* p){++*static_cast<unsigned*>(p);}, &callbacks)==OtaUpdater::OK);
  assert(flashCalls==1 && callbacks==2 && updater.getProcessedSize()==updater.getTotalSize());
  // A failed second check must clear the first candidate.
  metadataWorks=false; assert(updater.checkForUpdate()==OtaUpdater::HTTP_ERROR);
  assert(!updater.isUpdateNewer() && updater.installUpdate()==OtaUpdater::UPDATE_OLDER_ERROR);
  setup(); downloadImage[100]^=1; rejected(OtaUpdater::INTERNAL_UPDATE_ERROR);
  setup(); downloadImage.pop_back(); rejected(OtaUpdater::INTERNAL_UPDATE_ERROR);
  setup(); downloadWorks=false; rejected(OtaUpdater::HTTP_ERROR);
  setup(); failStageRead=true; rejected(OtaUpdater::INTERNAL_UPDATE_ERROR);
  setup(); partitionSize=60000; rejected(OtaUpdater::INTERNAL_UPDATE_ERROR); assert(downloadCalls==0);
  setup(); partitionAvailable=false; rejected(OtaUpdater::INTERNAL_UPDATE_ERROR); assert(downloadCalls==0);
  setup("1.7.3-yacp-xlarge"); rejected(OtaUpdater::WRONG_DEVICE_ERROR);
  setup("1.7.3-yacp-x4pro-tiny"); rejected(OtaUpdater::WRONG_DEVICE_ERROR);
  setup("1.7.2-yacp-tiny"); rejected(OtaUpdater::WRONG_DEVICE_ERROR);
  setup(); response.pop_back(); assert(updater.checkForUpdate()==OtaUpdater::JSON_PARSE_ERROR);
  setup(); response.replace(response.find("sha256:"),7,"sha512:"); assert(updater.checkForUpdate()==OtaUpdater::JSON_PARSE_ERROR);
  setup(); response.replace(response.find("https://github.com"),18,"http://github.com"); assert(updater.checkForUpdate()==OtaUpdater::JSON_PARSE_ERROR);
  setup("1.7.3-yacp-xlarge", "YACP-1.7.3-yacp-xlarge.bin"); assert(updater.checkForUpdate()==OtaUpdater::NO_UPDATE);
  setup(); flashResult=firmware_flash::Result::WRONG_BOARD;
  assert(updater.checkForUpdate()==OtaUpdater::OK && updater.installUpdate()==OtaUpdater::WRONG_DEVICE_ERROR);
  assert(flashCalls==1);
  std::puts("YACP OTA orchestration: passed (network, SD and flashing are in-memory stubs)");
}
