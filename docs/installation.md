---
title: Installation
nav_order: 14
---

# Installation

## Try YACP and go back

Curious about YACP and want an easy way back to your current setup? Keep a copy of your current firmware and set your original microSD card aside. Use a spare card with a few books for the test. Download the YACP `.bin` for your exact model, plug the reader in over USB, then select your model and choose **Custom .bin** in the [CrossPoint web flasher](https://crosspointreader.com/#flash-tools) (see [Web Installer](#web-installer) below). To go back, reinstall your previous firmware using its usual flashing method and put your original card back in.

## Web Installer

1. Download the appropriate `YACP-<version>-<variant>.bin` file from the
   [YACP releases page](https://github.com/Sichroteph/YACP/releases/latest).
2. Connect your Xteink X4 or X3 to your computer via USB-C and wake/unlock the device.
3. Go to <https://crosspointreader.com/#flash-tools> and choose your device.
4. Select **Custom .bin** from the options.
5. Choose the YACP `.bin` file you downloaded and click **Flash**.

To revert back to the official firmware, flash the latest official firmware from <https://crosspointreader.com/#flash-tools>.

## Command Line

These instructions are for macOS and Linux. Windows users should use the web installer.

Install `esptool`:

```sh
pip3 install esptool
```

Download the appropriate YACP binary from the
[YACP releases page](https://github.com/Sichroteph/YACP/releases/latest), then connect your device with USB-C.

Find the device port:

```sh
# Linux
dmesg | grep tty

# macOS
ls /dev/cu.*
```

Flash the firmware:

```sh
# Linux
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin

# macOS
esptool.py --chip esp32c3 --port /dev/cu.usbmodem2101 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

Replace the port and firmware path with your actual values.
