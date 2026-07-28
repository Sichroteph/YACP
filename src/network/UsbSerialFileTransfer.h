#pragma once

namespace UsbSerialFileTransfer {

enum class ProcessResult {
  None,
  ScreenshotRequested,
};

ProcessResult process(bool fileTransferAllowed);

}  // namespace UsbSerialFileTransfer
