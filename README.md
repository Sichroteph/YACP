<p align="center">
  <img src="docs/images/yacp/logo.png" alt="YACP continuous reading path logo" width="144">
</p>

<h1 align="center">YACP</h1>

<p align="center"><em>Yet Another CrossPoint.</em></p>

YACP is a personal, opinionated firmware for Xteink X3 and X4 readers. It is built for my own use and is primarily
concerned with battery life, rendering efficiency, and getting back into the current book quickly. Reading statistics
and a small autonomy view are deliberate exceptions to this narrow scope.

[Download the latest release](https://github.com/Sichroteph/YACP/releases/latest) |
[Build status](https://github.com/Sichroteph/YACP/actions/workflows/ci.yml)

The project has required substantial work across power handling, rendering, storage, memory use, and simulator
validation. The hardware and the objective remain simple: this is firmware for reading books. Major changes should
not be expected every week, and releases have no fixed cadence.

YACP is expected to use less energy and perform less incidental work than its baseline, but it does not publish a
measured battery-life claim yet. Long-running current analysis continues so future choices can be based on repeatable
hardware measurements rather than optimistic estimates.

## What is new in 1.4.0-yacp.155

- **No-flash X3 screen maintenance:** the OEM black-and-white reinforcement waveform can settle text ghosting during
  the page turn itself, without the full-screen black/white flash used by a full refresh.
- **A real ending for every book:** completing an EPUB or XTC now opens a Reading Achievement page with the book's
  reading time, sessions, average session, favorite reading period, reading span, and updated lifetime totals.
- **Finished Books history:** Reading Stats now exposes the 32 most recently completed books in five-entry pages,
  using a bounded SD index instead of scanning the whole card.

This release also adds a per-update profile chooser, improves Home refresh behavior, extends periodic maintenance to
1, 5, 10, 15, 30, or 60 pages (or Never), and includes smaller reliability and documentation fixes. See the
[changelog](CHANGELOG.md) for the complete inventory.

## Direction

In order, YACP aims to:

1. reduce energy used while a book is open and waiting for input;
2. remove unnecessary CPU, display, SD-card, network, and allocation work;
3. keep page turns, chapter changes, resume, and rendering responsive;
4. remain predictable on an ESP32-C3 with no PSRAM;
5. retain a few personally useful features, especially reading statistics.

The expected user normally reads one current book sequentially until completion, uses Quick Resume after automatic
sleep, uses a built-in font, and changes settings infrequently. Features may remain available when they add no cost
to that path.

## Origin

YACP starts from [CrossInk 1.4.0](https://github.com/uxjulia/CrossInk), itself based on
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). It keeps the compatible architecture,
storage layout, EPUB work, and broad reader baseline, then applies a smaller set of defaults and optimizations.

CrossPoint, CrossInk, and other Xteink firmware projects are reviewed regularly. A change is adopted only when it
reduces energy or work, improves reliability, or completes an operation faster. Newer or broader is not sufficient.

## The YACP theme

The custom YACP Home is a rounded, typographic reading surface inspired by RoundedRaff. It focuses on one current
book, showing its title, author, progress, reading time, and estimated time left.

<p align="center">
  <img src="docs/images/yacp/media/home-switcher.gif"
       alt="YACP Home switching between Alice in Wonderland at 46 percent and Sherlock Holmes at 24 percent"
       width="300">
  <br>
  <sub><strong>YACP default:</strong> Previous &middot; Next &middot; Read &middot; Menu</sub>
</p>

Home also acts as a lightweight book switcher. The Left and Right buttons move through the valid recent-book list
while the title, author, progress, and reading statistics update in place; the library never needs to open. Confirm
resumes whichever book is currently shown. At either end of the list, the unavailable direction is blank and does
nothing.

The design choices are functional:

- no cover loading, thumbnail generation, EPUB opening, or Home cover cache;
- Confirm resumes the current book directly;
- Left and Right switch directly to the previous or next valid recent book;
- Recent Books, Reading Stats, Autonomy, OPDS, transfer, and settings remain in the secondary menu;
- optional catalogue and statistics probes run only when their screen is requested;
- the first Home render uses one full refresh to clear ghosting from the gray card, then navigation uses fast
  refreshes;
- the centered pale-gray YACP mark reuses the existing 1-bit asset, with no duplicated bitmap, SD access, or runtime
  allocation.

Real page text is not reconstructed merely to decorate Home because that would add SD reads, deserialization, and
memory use. Other inherited themes remain available because an inactive theme has no reading-path cost.

After each firmware update, YACP offers its recommended `Left, Right, Confirm, Back` profile—the layout demonstrated
above—the familiar CrossInk profile, or preservation of every current setting. Upgrades preselect the safe
keep-current choice, while a fresh installation recommends YACP.

## Implemented direction

### Quiet reading path

- X3 stays awake at 10 MHz between unchanged 50 ms button polls. ESP light sleep is avoided because it can drop the
  power latch and prevent a button wake.
- A detected button edge receives a prompt confirmation sample without increasing the continuous polling rate.
- USB checks are rate limited, and the CPU can return to the lower frequency while the e-ink controller is busy.
- EPUB, TXT, and XTC progress writes are debounced, then flushed on normal reader exit.
- Automatic sleep defaults to Quick Resume. The X3 path avoids the full-screen black synchronization pass when
  entering sleep and restoring the cached page.

The X3 before and after recording shows the Quick Resume flashes removed by this path:

<video src="https://github.com/user-attachments/assets/a3de8027-e6e2-45ea-9f48-99801f550def" controls></video>

### No-flash X3 screen maintenance

Periodic screen maintenance can use the X3 OEM black-and-white reinforcement waveform instead of a flashing full
refresh. The reinforcement is the page turn itself, not a second display update, so unchanged black and white pixels
are settled without a full-screen flash. It can run every 1, 5, 10, 15, 30, or 60 pages, or periodic maintenance can
be disabled.

Required cleanup remains conservative: pages containing images or grayscale, indexing and popup residue, wake and
sleep transitions, manual full refreshes, and non-X3 devices continue to use the full-refresh path.

The release demonstration compares the previous behavior with the new waveform over the same ten-second reading
sequence. The same video adjustments are applied to both clips to make faint ghosting easier to see.

<video src="https://github.com/user-attachments/assets/06bacfcf-597c-4017-9074-67e3561071f8"
       controls></video>

[Open the no-flash screen-maintenance comparison](https://github.com/Sichroteph/YACP/releases/download/v1.4.0-yacp.155/YACP-ghosting-before-after.mp4)

### Rendering and storage

- Text antialiasing is off by default, avoiding the grayscale text pass while keeping image rendering enabled.
- Lexend Deca and Bitter are the normal built-in fonts.
- SD-card fonts remain available, but discovery, catalogue allocation, and file access begin only after explicit
  selection or font management. Temporary load failures use a built-in fallback without erasing the saved choice.
- The next EPUB chapter is prepared near the end of the current chapter, including one-page chapters and direct
  jumps to the last page. Memory guards can skip the work, and a prepared marker prevents repeated SD probes.
- Grayscale rendering reuses one bounded strip buffer per loaded section and releases it before chapter indexing.
- Existing low-memory EPUB fallbacks remain available for difficult books.

### Reading statistics

The first Reading Stats view combines the current book summary and this device's all-time figures on one screen,
making the most useful numbers readable without moving between two separate panels.

Reading Rhythm shows daily intensity, weekly reading time, reading days, and current and best streaks over the latest
12 months. Its daily history remains separate from the existing synchronized totals.

Finishing a new EPUB or XTC opens a Reading Achievement screen with the book's reading time, sessions, average session,
favorite reading period, reading span, completed-book count, and total reading time on the device. The following
Finished Books view keeps the 32 most recently completed books in a bounded SD index and exposes all retained entries
in five-book pages. Existing installations recover completed entries from their recent-books list without scanning
the entire SD card.

<table>
  <tr>
    <td align="center">
      <img src="docs/images/yacp/media/reading-achievement.png"
           alt="YACP Reading Achievement page shown after finishing Dune"
           width="264">
    </td>
    <td align="center">
      <img src="docs/images/yacp/media/finished-books.png"
           alt="YACP Books Finished history populated with deterministic sample books"
           width="264">
    </td>
  </tr>
  <tr>
    <td align="center">Reading Achievement</td>
    <td align="center">Books Finished</td>
  </tr>
  <tr>
    <td align="center">
      <img src="docs/images/yacp/media/reading-stats.png"
           alt="YACP Reading Stats summary combining the current book and this device on one screen"
           width="264">
    </td>
    <td align="center">
      <img src="docs/images/yacp/media/reading-rhythm.png"
           alt="Reading Rhythm rendered by the X3 simulator"
           width="264">
    </td>
  </tr>
  <tr>
    <td align="center">Reading Stats summary</td>
    <td align="center">Reading Rhythm</td>
  </tr>
</table>

These screenshots use deterministic generated data and contain no personal history. Reproduction commands are in the
[simulator guide](docs/simulator.md).

### Autonomy

Autonomy relates battery level to active use. It records one coarse point for each 5 percentage-point drop during the
existing transition to sleep, reuses a cached battery value when possible, and stores at most 21 points in 96 bytes.
It adds no timer or periodic wake-up.

<p align="center">
  <img src="docs/images/yacp/media/autonomy.png" alt="Battery tracking rendered by the X3 simulator" width="264">
</p>

The screenshot uses deterministic generated data and contains no personal history. Reproduction commands are in the
[simulator guide](docs/simulator.md).

## Baseline and scope

YACP carries a large CrossInk baseline and does not present inherited work as original. This includes EPUB reliability
work, reader controls, bookmarks, clippings, synchronization, render fallbacks, web file management, and the original
statistics system. The [changelog](CHANGELOG.md) is the detailed inventory.

X3 is the primary optimization target. X4 builds remain available, and device-specific behavior is guarded. Built-in
reader fonts are limited to Lexend Deca and Bitter; release builds are limited to `tiny` and `xlarge`.

## Download and install

Download binaries and checksums from [GitHub Releases](https://github.com/Sichroteph/YACP/releases/latest).

| File | Intended font sizes |
| --- | --- |
| `YACP-<version>-tiny.bin` | 10, 12, 14, and 16 pt |
| `YACP-<version>-xlarge.bin` | 16, 18, and 20 pt |

See the [installation guide](docs/installation.md) before flashing. A successful build proves compilation only, so
the release notes state the hardware-validation status of each binary.

## Build

PlatformIO is the source of truth:

```sh
pio run -e tiny -e xlarge
pio run -e simulator
```

Every firmware build uses a new `crossink_version`. The release workflow builds both variants, generates SHA-256
checksums, and applies the [build checklist](BUILD_CHECKLIST.md).

## Project status

This is not a community project. Issues, Discussions, pull requests, feature requests, support requests, and project
contact are not accepted. External pull requests are closed automatically. The source is public for inspection and
forking; maintain a fork if a different direction is needed.

Optional financial support does not buy features or influence project decisions:
[PayPal](https://www.paypal.com/paypalme/ChrJeannette).

## License

YACP retains the upstream project license. See [LICENSE](LICENSE).
