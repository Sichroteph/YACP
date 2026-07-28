---
title: Simulator
nav_order: 15
---

# Development device simulator

YACP uses the [CrossPoint simulator](https://github.com/uxjulia/crosspoint-simulator) for repeatable UI checks and screenshots. It compiles the firmware as a native host program and renders the e-ink framebuffer in SDL2.

The simulator does not reproduce power consumption, ESP32 timing, SD-card single-reader limits, or physical e-ink waveforms. Those checks still require hardware.

## Supported host paths

The current YACP configuration is used on Linux or WSL. Native Windows is not configured.

For Ubuntu or WSL:

```sh
sudo apt install libsdl2-dev libssl-dev
```

Install PlatformIO Core, then build one of the host environments:

```sh
pio run -e simulator
pio run -e simulator_x3
```

`simulator_x3` enables the X3 panel size and RTC behavior used by Reading Rhythm.

## Simulator SD card

The simulator maps `./fs_` to the device SD-card root. For example:

```text
./fs_/books/
./fs_/.crosspoint/
```

The directory is ignored by Git.

## Reading statistics demo

Generate deterministic data without using personal reading history:

```sh
python3 scripts/generate_reading_stats_demo.py --force
```

The generator writes only:

```text
fs_/.crosspoint/global_stats.bin
fs_/.crosspoint/daily_reading.bin
```

It refuses to replace those files unless `--force` is present.

## Run

```sh
pio run -e simulator_x3 -t run_simulator
```

Keyboard controls:

| Key | Action |
| --- | --- |
| Up / Down | Side buttons |
| Left / Right | Front buttons |
| Return | Confirm |
| Escape | Back |
| P | Power |
| S | Simulated sleep |

## Automated navigation and screenshots

The simulator accepts timed inputs and BMP capture paths:

```sh
mkdir -p qa-artifacts
CROSSPOINT_SIM_INPUT_SCRIPT='2500:DOWN;3000:DOWN;3500:ENTER;4700:RIGHT;6500:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='5600:./qa-artifacts/reading-rhythm.bmp' \
  .pio/build/simulator_x3/program
```

The committed Reading Rhythm image was produced from this path and converted to PNG.

## Cache note

First open of an EPUB builds data under `fs_/.crosspoint/`. Clear only the relevant simulator cache when validating parser, layout, image, or cache-format changes.
