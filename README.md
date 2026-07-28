<p align="center">
  <img src="docs/images/yacp/logo.png" alt="YACP continuous reading path logo" width="144">
</p>

<h1 align="center">YACP</h1>

<p align="center"><em>Yet Another CrossPoint.</em></p>

YACP is a personal, opinionated firmware for Xteink X3 and X4 readers. It is built for my own use. Its main priorities are battery life and rendering efficiency. Reading statistics and a small autonomy view are the deliberate exceptions to an otherwise narrow feature policy.

The expected result is longer time between charges and more consistent performance in the normal reading path. The mechanisms are concrete: lower idle CPU frequency, fewer display synchronization passes, fewer SD-card scans and writes, deferred optional work, smaller caches, and reused allocations. YACP does not yet publish an end-to-end battery-life percentage because comparable hardware measurements are still needed.

This direction has required substantial work across power handling, rendering, storage, memory use, and simulator validation. The hardware and the goal remain deliberately simple: this is firmware for reading books. Major breakthroughs should not be expected every week, and releases do not follow a fixed cadence. A change is released when it provides a relevant, explainable improvement.

In parallel, long-running hardware current analysis remains active. Its purpose is to compare parameters and mechanisms under repeatable use, then keep only changes that measurably reduce battery use without weakening reliability. This is an iterative process, so further power-related changes are expected as the measurements mature.

This is not a community project. Issues, pull requests, feature requests, support requests, and project contact are not accepted at this time. The source is public so the work can be inspected or forked.

[![Build firmware](https://github.com/Sichroteph/YACP/actions/workflows/ci.yml/badge.svg)](https://github.com/Sichroteph/YACP/actions/workflows/ci.yml)

## Origin

YACP currently starts from [CrossInk 1.4.0](https://github.com/uxjulia/CrossInk), which is itself based on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).

CrossPoint provides the core open source reader firmware and hardware support. CrossInk adds a broad reader feature set, typography work, statistics, synchronization, and many EPUB reliability fixes. YACP keeps that compatible architecture and narrows its direction around a smaller set of priorities.

YACP does not follow either upstream automatically. CrossPoint, CrossInk, and other Xteink firmware projects are reviewed regularly. A change is considered when it reduces energy use, reduces work, improves rendering or reliability, or makes an operation complete faster. General feature growth is not a reason by itself.

The existing `.crosspoint` SD-card layout and cache formats are intentionally retained where possible.

## Priorities

In order:

1. Reduce energy used while a book is open and waiting for input.
2. Reduce unnecessary CPU, display, SD-card, and allocation work.
3. Make page turns, chapter changes, resume, and rendering feel immediate.
4. Keep failure behavior predictable on an ESP32-C3 with no PSRAM.
5. Keep a small number of features that are personally useful, especially reading statistics.

There is no target feature count and no goal of serving every reading workflow.

## Opinionated default path

YACP assumes one current book is normally read in sequence until completion. A clean profile therefore uses the
YACP theme: a rounded, typographic Home centered on that one book, its progress, reading time, and estimated time
left. It deliberately omits the cover, thumbnail generation, and a Home cover cache. Confirm resumes directly;
the other mapped buttons open Menu or move to the previous/next book in most-recent-first opening order without a
Home-screen cursor. A direction with no book is left blank and does nothing. Recent Books and Reading Stats remain
available from Menu. OPDS and saved-item probes happen only when the menu is requested; global statistics load only
for that menu or the Reading Stats screen.
Automatic sleep uses Quick Resume by default, while a deliberate manual sleep keeps its independently selected sleep
screen.

Text antialiasing is disabled by default to avoid the grayscale text pass; image rendering remains enabled. Lexend
Deca and Bitter are the normal reader fonts. SD-card fonts remain available as a comfort option, but their directories
and catalogue are loaded only after an SD family is selected or font management is explicitly opened.

The next EPUB chapter is prepared near the end of the current chapter because sequential continuation is the expected
path. Settings are assumed to be stable and rarely changed; YACP does not add write batching solely to optimize
settings experimentation.

## Current YACP work

### Idle power and writes

- X3 readers stay awake at 10 MHz while quiet, using the existing 50 ms ADC button polling cadence. A raw button edge receives a confirming sample after 10 ms. ESP light sleep is avoided because it can drop the board's power latch and prevent a page button from waking the device.
- The CPU may run at the lower power frequency while the e-ink controller is busy with a refresh.
- X3 USB state checks are rate limited while idle instead of being repeated on every loop.
- EPUB, TXT, and XTC progress writes are debounced. A position is persisted after 10 changes or 5 minutes, with the latest pending position flushed on normal reader exit.

The lower quiet frequency and reduced polling work should lower awake idle consumption. Debounced progress writes reduce SD-card activity and repeated serialization. These mechanisms are implemented, but hardware current and runtime measurements remain part of the validation work.

### Autonomy history

- The Autonomy screen shows battery level against active use, active reading time since the current charge cycle, and elapsed days when a valid clock is available.
- Awake time is accumulated only while external power is disconnected.
- One coarse battery point is recorded for each 5 percentage-point drop, with a maximum of 21 points in a fixed state of at most 96 bytes.
- Sampling happens during the existing transition to sleep and reuses a cached battery value when one is available. The state is written with the existing application-state save.
- The feature adds no timer and no periodic wake-up. Reading the clock for elapsed days happens only when the Autonomy screen is opened.

This view is an observability feature, not a claimed power saving. Its implementation is deliberately small so measuring autonomy does not materially interfere with it. The simulator provides a deterministic 10-day demonstration:

![Battery tracking rendered by the X3 simulator with generated demo data](docs/images/yacp/autonomy.png)

The graph relates battery level to active use rather than calendar time. The capture uses generated demonstration
data, not personal battery history. Recreate the demo with:

```sh
CROSSINK_SIM_POWER_DEMO=1 pio run -e simulator_x3 -t run_simulator
```

### Rendering and resume

- X3 Quick Resume avoids the full-screen black synchronization pass when entering sleep and when restoring a cached reader page.
- EPUB reading starts indexing the next chapter silently near the end of the current chapter. This also covers one-page chapters and direct jumps to the last page. A prepared marker avoids repeating the same SD-card probes, and indexing is skipped when the optional-work memory budget is not available.
- EPUB grayscale rendering allocates one bounded strip buffer per loaded section, reuses it for each page, and releases it before chapter indexing.
- The Home carousel keeps one rendered frame in RAM and pages other snapshots from SD.
- The YACP Home does not load or cache a cover. Minimal still caches only its rendered cover region instead of the
  full Home tile.
- Each YACP Home load performs one full e-ink refresh to clean ghosting from the gray book surface; later book
  selections and returns from secondary screens use fast refreshes. The centered YACP mark is the existing embedded
  1-bit asset, so it adds no SD access or runtime allocation.
- Low-memory EPUB fallbacks inherited from CrossInk remain enabled for difficult books, large publisher styles, custom SD-card fonts, and image-heavy sections.

X3 before / after recording of the full-screen Quick Resume flashes removed by this path:
<video src="https://github.com/user-attachments/assets/a3de8027-e6e2-45ea-9f48-99801f550def" controls></video>

### Deferred optional work

- Clean profiles default to YACP, text antialiasing off, and Quick Resume after automatic sleep. Existing valid user choices are preserved.
- Disabling text antialiasing avoids the grayscale text pass. Image rendering remains enabled.
- Built-in Lexend Deca and Bitter fonts do not trigger an SD-card font-directory scan, catalogue allocation, or font-file access.
- SD-card font discovery and loading begin only when an SD family is selected or a font picker, manager, or web selector is explicitly opened. The catalogue is released when it is no longer needed.
- The YACP Home has no cover-generation path. Themes that still generate Home covers use built-in fonts so optional
  SD font work is not activated there.

These choices remove filesystem and allocation work from the common built-in-font reading path while retaining SD-card fonts as an explicit comfort option.

### Reading statistics

- Book and all-book totals share a compact summary page.
- Reading Rhythm adds a daily intensity heatmap, weekly reading time, current and best streaks, and reading days across the latest 12 months.
- Daily intensity is stored separately from the existing global statistics payload so nearby statistics sync remains compatible.

![Reading Rhythm rendered by the X3 simulator with generated demo data](docs/images/yacp/reading-rhythm.png)

The screenshot uses generated demonstration data, not personal reading history. Recreate it with:

```sh
python3 scripts/generate_reading_stats_demo.py --force
pio run -e simulator_x3 -t run_simulator
```

### Deliberately narrower choices

- Built-in reader fonts are limited to Lexend Deca and Bitter.
- YACP keeps the `tiny` and `xlarge` build variants and does not produce Teensy variants.
- New upstream features are evaluated against the energy and execution criteria above.

## What comes from CrossInk

YACP carries a large CrossInk baseline. It does not claim those features as original YACP work. Important inherited areas include:

- reading statistics for EPUB, TXT, XTC, and XTCH;
- Nearby Position Sync and Nearby Stats Sync;
- per-book EPUB render modes and low-memory fallback modes;
- custom reader controls, bookmarks, clippings, reading aids, and finished-book tracking;
- web file management and EPUB optimization;
- EPUB parser, image, font, cache, and right-to-left rendering fixes.

The [changelog](CHANGELOG.md) is the detailed inventory. Entries in the current YACP version include both carried CrossInk work and YACP-specific changes. The distinction is stated in release notes when it matters.

## Hardware scope

The X3 is the primary optimization target because its ADC button ladder and display behavior expose the largest idle and resume opportunities. X4 builds remain available. Device-specific behavior is guarded so an X3 optimization does not silently change the X4 path.

YACP runs on an ESP32-C3 with no PSRAM and roughly 380 KB of usable RAM. Stability takes priority over convenience.

## Build and release

PlatformIO is the source of truth:

```sh
pio run -e tiny
pio run -e xlarge
pio run -e simulator_x3
```

Every firmware compilation must use a new `crossink_version` in `platformio.ini`. User-facing files follow this form:

```text
YACP-<crossink_version>-yacp-tiny.bin
YACP-<crossink_version>-yacp-xlarge.bin
```

Each push to `main` is expected to:

- change the embedded version;
- update the top changelog entry;
- build both firmware variants;
- report firmware size and the project checklist;
- attach checksums and binaries to a draft GitHub release.

See [BUILD_CHECKLIST.md](BUILD_CHECKLIST.md) for the exact gate.

## Installation status

This repository is a working firmware project, not a supported distribution channel. A successful CI build only proves compilation. Before using a binary, check the matching release notes and hardware-validation status.

## Contribution status

External contributions are closed. GitHub Issues and Discussions are disabled, and external pull requests are closed automatically. There is no support or contact channel for YACP.

If the code is useful, fork it and maintain the result for your own device. See [CONTRIBUTING.md](CONTRIBUTING.md) for the short policy.

If YACP is useful to you, you can optionally [support its independent development](https://www.paypal.com/paypalme/ChrJeannette). Support helps with maintenance and hardware testing, but does not buy features or influence project decisions.

## License

YACP retains the upstream project license. See [LICENSE](LICENSE).
