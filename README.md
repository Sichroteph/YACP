<p align="center">
  <img src="docs/images/yacp/logo.png" alt="YACP continuous reading path logo" width="144">
</p>

<h1 align="center">YACP</h1>

<p align="center"><em>Yet Another CrossPoint.</em></p>

YACP is a personal, opinionated firmware for Xteink X3 and X4 readers. It is built for my own use and is primarily
concerned with getting back into the current book quickly, avoiding unrelated SD-card work on that path, and improving
battery and rendering efficiency. Reading statistics and a small autonomy view complement that focus.

YACP is built for one primary path: open a book, keep the reader responsive, use as little battery and incidental work
as practical, and read sequentially until the book is finished.

[Download the latest release](https://github.com/Sichroteph/YACP/releases/latest) |
[Build status](https://github.com/Sichroteph/YACP/actions/workflows/ci.yml)

Development spans power handling, rendering, storage, memory use, and simulator validation. Releases have no fixed
cadence.

YACP is designed to use less energy and perform less incidental work than its baseline, but it does not publish a
measured battery-life claim yet. Long-running current analysis continues until repeatable hardware measurements are
available.

## Direction

In order, YACP aims to:

1. reduce energy used while a book is open and waiting for input;
2. remove unnecessary CPU, display, SD-card, network, and allocation work;
3. keep page turns, chapter changes, resume, and rendering responsive;
4. remain predictable on an ESP32-C3 with no PSRAM;
5. retain a few personally useful features, especially reading statistics.

Current optimizations primarily target long sequential reading sessions, Quick Resume after automatic sleep, built-in
fonts, and infrequent settings changes. Features that add no work to this path can remain available.

## Origin

YACP starts from [CrossInk 1.4.0](https://github.com/uxjulia/CrossInk), itself based on
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). It keeps the compatible architecture,
storage layout, EPUB work, and broad reader baseline, then applies a smaller set of defaults and optimizations.

Ideas from CrossPoint, CrossInk, and other Xteink firmware projects are reviewed regularly and adapted when they fit
YACP's focus on efficiency, reliability, or responsiveness.

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

### Faster X3 wake and lower-I/O sleep

The normal X3 reading cycle is treated as a fast path: wake, restore the current page, and go back to sleep without
starting unrelated subsystems or touching SD-card files that do not help display that page. An ESP32 deep-sleep wake is
a fresh boot, so startup work removed here directly reduces the time needed to resume a book. This is the normal Quick
Resume behavior rather than an optional performance mode.

| Stage | Work avoided or deferred | Practical effect |
| --- | --- | --- |
| Battery-powered boot | USB CDC starts only when a cable is detected or Home is opened | Removes the fixed 250 ms USB enumeration wait from the normal X3 wake |
| Boot-button detection | Two stable physical samples 8 ms apart replace the roughly 500 ms settling wait | Removes about another 492 ms of fixed startup delay while retaining chord detection |
| Direct book resume | The retained framebuffer is reused; a valid cached EPUB section is prepared without rendering the visible page again | The existing reading page remains visible and becomes interactive sooner |
| SD-card work at resume | The redundant reader-entry state write and recent-book update are skipped; Recent Books, OPDS, and KOReader credentials load only on first use; page metadata such as footnotes waits until requested | Avoids unrelated SD reads and writes on the common path back into the book |
| Return to sleep | An identical same-day automatic statistics backup is not rewritten, and the panel controller switches off as soon as its localized refresh completes | Avoids an unnecessary SD rewrite and stops powering the display electronics earlier |

These changes reduce, rather than eliminate, SD activity: progress, statistics, the cached section, and the state writes
that protect against a failed-resume boot loop remain. A stale or missing EPUB cache also keeps the normal render and
indexing fallback.

The visual path is optimized at the same time. Automatic sleep defaults to Quick Resume, the X3 avoids the full-screen
black synchronization pass, and the small wake marker is removed with a localized no-flash update. Waking from an
image-based sleep screen restores the pre-sleep framebuffer instead of displaying the normal splash.

The rest of the reading loop follows the same principle:

- X3 stays awake at 10 MHz between unchanged 50 ms button polls. ESP light sleep is avoided because it can drop the
  power latch and prevent a button wake.
- A detected button edge receives a prompt confirmation sample without increasing the continuous polling rate.
- USB checks are rate limited, and the CPU can return to the lower frequency while the e-ink controller is busy.
- EPUB, TXT, and XTC progress writes are debounced, then flushed on normal reader exit.
- Either side button can optionally turn back one page on a long press without changing its normal short-press page
  direction; one hold triggers one turn and must be released before another.

One hardware recording per configuration produced the following indicative wake timings, measured from the physical
Power click until the loading dots were replaced by the battery indicator (approximately +/- 0.1 s):

| Wake path | Updated firmware | Previous firmware | Difference |
| --- | ---: | ---: | ---: |
| Quick Resume to retained page | ~1.3 s | ~1.8 s | ~0.5 s faster (~27% less delay) |
| Cover sleep image to reader | ~2.8 s | ~3.4 s | ~0.6 s faster (~19% less delay) |

These single-run observations are not a formal benchmark or a battery-life claim. The implementation removes about
742 ms of fixed X3 startup waits, but some of that time overlaps other startup work and should not be added directly to
the end-to-end figures.

<p align="center">
  <img src="https://github.com/Sichroteph/YACP/releases/download/v1.6.0-yacp/YACP-x3-faster-cover-wake-preview.gif"
       alt="An X3 waking from a book-cover sleep image into the current reading page"
       width="480">
  <br>
  <sub>GIF preview of the latest X3 Cover-wake recording from the hardware test session.
  <a href="https://github.com/Sichroteph/YACP/releases/download/v1.6.0-yacp/YACP-x3-faster-cover-wake.mp4">Download the unedited original MP4.</a></sub>
</p>

Boot chords use physical button positions, so custom reader mappings do not change them:

| Hold while pressing Power | Result | Devices |
| --- | --- | --- |
| Front button No. 1 (leftmost in portrait) | Safe Home: bypass Quick Resume and open through the normal splash and Home path | X3 |
| Left side button | Firmware update | X3 and X4 |
| Right side button | Join Network / File Transfer | X3 and X4 |

The earlier X3 before-and-after recording below documents the removal of full-screen Quick Resume flashes. It is
separate from the current wake-timing recording above:

<video src="https://github.com/user-attachments/assets/a3de8027-e6e2-45ea-9f48-99801f550def" controls></video>

### No-flash X3 screen maintenance

Periodic screen maintenance can use the X3 OEM black-and-white reinforcement waveform instead of a flashing full
refresh. The reinforcement is the page turn itself, not a second display update, so unchanged black and white pixels
are settled without a full-screen flash. It can run every 1, 5, 10, 15, 30, or 60 pages, or periodic maintenance can
be disabled.

Text-only EPUB and TXT pages remain eligible when font anti-aliasing is enabled: dense stroke pixels stay black while
only lighter edge pixels use gray. Required cleanup remains conservative. Pages containing images, indexing and popup
residue, wake and sleep transitions, manual full refreshes, and non-X3 devices continue to use the full-refresh path.

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

### Wi-Fi and transfer

With several saved networks, Join Network scans once and sorts the visible saved SSIDs by signal strength. If the
strongest candidate cannot connect, YACP silently tries the next one instead of showing an error and asking whether to
forget valid credentials. The scan's channel and BSSID are passed into the connection attempt, avoiding a second scan
inside the Wi-Fi driver. A single saved network keeps the faster direct path, and `Show` interrupts automatic attempts
when the full network list is wanted immediately.

<p align="center">
  <img src="https://github.com/Sichroteph/YACP/releases/download/v1.6.0-yacp/YACP-wifi-multi-network-before-after.gif"
       alt="Previous and updated saved Wi-Fi selection paths when the last-used network is unavailable"
       width="760">
  <br>
  <sub>The updated path advances through available saved networks without an error or credential-deletion prompt.
  <a href="https://github.com/Sichroteph/YACP/releases/download/v1.6.0-yacp/YACP-wifi-multi-network-before-after.mp4">Download the MP4.</a></sub>
</p>

### Adaptive images and sleep galleries

Sleep images use adaptive tonal analysis before four-level dithering. A fixed mapping can wash out highlights or
collapse useful midtones when the source occupies an unusual tonal range; analyzing each image first makes better use
of the shades the panel can physically display.

<p align="center">
  <img src="https://github.com/Sichroteph/YACP/releases/download/v1.5.0-yacp/YACP-sleep-image-before-after.jpg"
       alt="Four real-device before and after comparisons of YACP adaptive grayscale sleep-image rendering"
       width="760">
  <br>
  <sub>The same source images rendered with the previous and adaptive mappings on a real X3.</sub>
</p>

Prepared images can be attached to a particular book or kept in the global sleep-image collection. When several are
available for the current book, YACP rotates them without immediately repeating the previous choice. Converted planes
are cached on the SD card and reused while the source remains unchanged. Percentile selection rounds tiny sample sets
up to a valid source pixel, so the adaptive mapping also remains defined for very small images.

The browser workflow can crop an image, resize it for X3 or X4, preview the actual four-level conversion, adjust its
thresholds, and upload the reader-ready BMP. A dedicated Sleep Images page manages the global collection and one
selected book gallery without exposing their hidden storage folders.

https://github.com/user-attachments/assets/9954121c-3b27-4330-a455-f3d60a4063e7

<p align="center">
  <sub>Complete workflow: see the per-book result on the reader, then prepare and attach an image from the browser.
  <a href="https://github.com/Sichroteph/YACP/releases/download/v1.5.0-yacp/YACP-sleep-images-overview.mp4">Download the original-quality video.</a></sub>
</p>

### Reading statistics

The first Reading Stats view combines the current book summary and this device's all-time figures on one screen,
making the most useful numbers readable without moving between two separate panels.

Reading Rhythm shows daily intensity, weekly reading time, reading days, and current and best streaks over the latest
12 months. Its daily history remains separate from the existing synchronized totals.

Crossing the real end of an EPUB or XTC completes it automatically. Leaving from its final page or while its rounded
progress reads 100% asks for confirmation instead, so a direct jump or trailing material cannot silently finish the
book. The first completion date stays unchanged when the book is reopened or reread.

Finishing a new book opens a Reading Achievement screen with its reading time, sessions, average session, favorite
reading period, reading span, completed-book count, and total reading time on the device. The following Finished Books
view keeps the 32 most recently completed books in a bounded SD index and groups them by finish month, showing each
month's book count, reading time, titles, and authors. Existing installations recover completed entries from their
recent-books list without scanning the entire SD card. Opening Reading Stats also reconciles a completed book's
persistent dates and author with its retained entry, so legacy entries repair themselves without a fake edit.
Opening the view from a long-press OK shortcut consumes the originating button release once, so that release cannot
immediately close the statistics screen again.

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
It also counts successfully displayed EPUB, TXT, and XTC reader pages during the current battery cycle, in both
directions. The count is updated in RAM and persisted with the existing sleep-state save, so it adds no per-page SD
write, timer, or periodic wake-up. X3 users doing power measurements can optionally add average battery current in mA
to the reader status bar with the `Battery Current` setting.

<p align="center">
  <img src="docs/images/yacp/media/autonomy.png" alt="Battery tracking rendered by the X3 simulator" width="264">
</p>

The screenshot uses deterministic generated data and contains no personal history. Reproduction commands are in the
[simulator guide](docs/simulator.md).

## Baseline and scope

YACP inherits a substantial CrossInk baseline, including EPUB reliability work, reader controls, bookmarks, clippings,
synchronization, render fallbacks, web file management, and the original statistics system. The
[changelog](CHANGELOG.md) is the detailed inventory.

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

For automated developer workflows, a firmware file can first be uploaded to the SD card through File Transfer, then
validated and installed through `POST /api/firmware/install`. The endpoint requires explicit confirmation, checks the
image before writing the inactive OTA partition, and restarts only after a successful flash.

## Build

PlatformIO is the source of truth:

```sh
pio run -e tiny -e xlarge
pio run -e simulator
```

Every firmware build uses a new `crossink_version`. The release workflow builds both variants, generates SHA-256
checksums, and applies the [build checklist](BUILD_CHECKLIST.md).

## Project status

GitHub Issues are intended for reproducible bug reports, hardware feedback, and focused technical discussion.
Development follows the project's current priorities; alternative directions can be explored through forks.

Financial support is optional and does not affect those priorities:
[PayPal](https://www.paypal.com/paypalme/ChrJeannette).

## License

YACP retains the upstream project license. See [LICENSE](LICENSE).
