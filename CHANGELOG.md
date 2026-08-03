## [v1.5.0-yacp] - 2026-08-03

### Added

- Added a dedicated Sleep Images web page for managing global sleep images and the gallery attached to one selected
  book without exposing the hidden SD-card folders.
- Added per-book image galleries. Compatible gallery BMPs can be opened from the reader menu and used as sleep images
  when sleep starts from that book.
- Added browser-side image preparation to File Transfer, with visual crop selection, X3/X4 target sizing, four-level
  grayscale preview, threshold tuning, and reader-ready BMP generation before upload.
- Added `POST /api/firmware/install` for developer workflows. After a firmware is uploaded to the SD card, the endpoint
  validates the image, flashes the inactive OTA partition, and restarts the reader.
- Added `Previous Page` as an optional side-button long-press action in EPUB, TXT, and XTC readers.

### Changed

- Sleep-image rendering now analyzes each image's tonal range before dithering, preserving substantially more useful
  highlight and midtone detail on the four-level e-ink output.

<p align="center">
  <img src="https://github.com/Sichroteph/YACP/releases/download/v1.5.0-yacp/YACP-sleep-image-before-after.jpg"
       alt="Four real-device before and after comparisons of legacy and adaptive YACP sleep-image rendering"
       width="760">
</p>

- Rendered grayscale sleep-image planes are cached on the SD card and reused while the source image remains unchanged.
- Per-book sleep images take priority in Cover, Custom, and Cover/Custom modes, rotate randomly, and avoid immediately
  repeating the previous image when several prepared BMPs are available.
- Waking after an image-based sleep screen restores the pre-sleep framebuffer instead of showing the normal boot
  splash. This implementation was inspired by CrossPoint PR #2731 by Marek Vesely (`notmarek`).
- Finished Books now groups completed titles by finish month and shows each month's book count, reading time, titles,
  and authors.

### Removed

- Removed the temporary on-device grayscale comparator and browser sorting tools used while validating the new image
  pipeline.

### Fixed

- Reaching the real EPUB or XTC ending now completes the book automatically, while leaving from the final page or a
  rounded 100 percent asks for confirmation.
- First completion dates remain stable across rereads, pending Reading Achievement screens survive activity replacement
  or restart, and legacy Finished Books entries reconcile their dates and authors from persistent statistics.
- Sleep-image management loads gallery data and previews only when requested, reducing SD-card contention during file
  uploads.
- File Transfer keeps the selected book-gallery destination explicit throughout image preparation, reducing accidental
  uploads to the current folder.
- Quick Resume back into the reader restores the loading-dot indicator before live battery status replaces it.

## [v1.4.0-yacp.226] - 2026-08-02

### Changed

- Simplified the Sleep Images page around default images and one selected book, with contextual add actions instead
  of technical folder shortcuts.
- Adding an image from the Sleep Images page now opens the existing image preparation workflow with the correct
  destination and selected book already set, then returns to Sleep Images after a successful upload.

## [v1.4.0-yacp.225] - 2026-08-02

### Added

- Added a dedicated Sleep Images web page for global sleep images and per-book galleries. File Manager no longer loads
  sleep-image lists when managing regular files.
- File Transfer can still upload directly into the hidden `/.sleep` folder when that folder is opened explicitly.

### Removed

- Removed the temporary sleep-image test UI and browser sorting page used during grayscale tuning.

### Fixed

- Sleep-image management now loads gallery contents and image previews only on demand, reducing SD-card contention
  during uploads.
- Quick Resume back into the reader now shows the small loading-dot indicator in the bottom-left corner again before
  the live battery status replaces it.
- Per-book gallery sleep-image rotation now avoids immediately repeating the previously used BMP when several
  prepared images are available for the book.

## [v1.4.0-yacp.214] - 2026-08-02

### Fixed

- File Transfer now keeps the chosen book-gallery destination clearer and less fragile, so prepared BMP uploads are
  less likely to be sent to the current folder by mistake.

## [v1.4.0-yacp.211] - 2026-08-02

### Fixed

- Per-book gallery sleep-image rotation now uses the ESP32 hardware RNG, avoiding the same first image after each
  deep-sleep boot.

## [v1.4.0-yacp.210] - 2026-08-02

### Fixed

- Per-book gallery sleep images now choose randomly among the book's prepared BMPs instead of always using the first
  uploaded image.

## [v1.4.0-yacp.209] - 2026-08-02

### Changed

- Book gallery BMPs now take priority as the sleep image when sleep starts from that book in Cover, Custom, or
  Cover/Custom sleep modes.

## [v1.4.0-yacp.208] - 2026-08-02

### Added

- File Transfer's e-reader image preparation now exposes three grayscale threshold sliders, so uploads can be tuned
  before generating the 2-bit BMP used on the reader.

## [v1.4.0-yacp.206] - 2026-08-02

### Added

- File Transfer can now attach images to a per-book gallery. The reader menu shows `Book Gallery` only for books with
  compatible gallery images, and opens the existing image viewer for that book's gallery.
- File Transfer can now prepare uploaded images for the e-reader in the browser, with crop selection, X3/X4 target
  sizing, four-level grayscale dithering, and 2-bit BMP output before upload.

### Changed

- Dropping or selecting images in File Transfer now uploads the originals by default; e-reader crop/resize preparation
  is an explicit opt-in choice.
- Quick resume now shows a wake-up label over the restored sleep frame only when waking to Home, keeping direct
  reader resumes visually quiet.

## [v1.4.0-yacp.200] - 2026-08-02

### Changed

- Custom BMP sleep images now use adaptive tonal analysis before dithering, preserving more useful detail while
  keeping the corrected four-level grayscale mapping.
- Rendered custom sleep-image grayscale planes are cached on the SD card and reused when the source image has not
  changed.
- Added a temporary main-menu diagnostic screen for comparing legacy and adaptive sleep-image grayscale rendering
  directly on device, with a taller label badge and Confirm mapped to the next image.
- Fixed the temporary sleep-image diagnostic so it uses the same white BW base and crop ratios as the real sleep-image
  renderer, and keeps the label out of the grayscale planes.
- The temporary sleep-image diagnostic now uses English overlay labels, `Before` and `After`, regardless of UI
  language.
- The temporary sleep-image diagnostic now uses Left/Right for the linear Before/After sequence, while Confirm jumps
  to the next image's `Before` frame.
- Added a lightweight web Sleep Images page for previewing sleep-screen images and sorting them into `keep` or
  `reject` folders from the browser.

## [v1.4.0-yacp.185] - 2026-08-01

### Changed

- Removed the temporary real-device sleep-image comparison mode from the firmware build.

## [v1.4.0-yacp.184] - 2026-08-01

### Changed

- The temporary sleep-image grayscale comparison build now opens the real-device comparator automatically at boot,
  without requiring a button combo.

## [v1.4.0-yacp.183] - 2026-08-01

### Added

- Added a hidden real-device diagnostic mode to compare the legacy and corrected sleep-image grayscale mappings on
  every BMP in `/.sleep`.

## [v1.4.0-yacp.181] - 2026-08-01

### Fixed

- Waking from image-based sleep screens to Home now reuses the wake cleanup refresh and avoids a second strong Home
  refresh.

## [v1.4.0-yacp.180] - 2026-08-01

### Fixed

- Waking from image-based sleep screens now uses the wake half-refresh as the cleanup pass and avoids a second half
  refresh when the reader page opens.

## [v1.4.0-yacp.179] - 2026-08-01

### Changed

- Rebuilt the Tiny firmware with the remote File Transfer firmware-install endpoint and the current sleep-cover
  grayscale correction.

## [v1.4.0-yacp.178] - 2026-08-01

### Fixed

- Sleep cover grayscale generation now keeps more midtone detail instead of pushing many light-mid tones to white.

## [v1.4.0-yacp.176] - 2026-08-01

### Added

- File Transfer now exposes a remote firmware install endpoint so a developer can upload a `.bin` over HTTP, trigger
  validation and OTA flashing from the computer, and let the reader restart without navigating the firmware menu.

## [v1.4.0-yacp.174] - 2026-08-01

### Changed

- Sleep wake now skips the boot splash after every sleep screen mode by restoring the pre-sleep frame first; X3 keeps
  the no-flash wake indicator only after true Quick Resume sleep screens and uses a half refresh after image-based
  sleep screens.

## [v1.4.0-yacp.168] - 2026-08-01

### Changed

- Finished Books now groups completed titles by finish month, showing each month's book count, total reading time,
  titles, and authors instead of exact per-book date timelines.

## [v1.4.0-yacp.167] - 2026-08-01

### Added

- The side-button long-press setting now offers Previous Page, so either side button can return one page while its
  short press keeps the configured page-turn direction in EPUB, TXT, and XTC readers. The action fires once when the
  hold threshold is reached and requires a release before it can fire again.

### Changed

- Finished Books now presents each known reading span as a compact start-to-finish timeline with the year on both
  dates and the active reading time. The redundant elapsed-day count is omitted, and legacy indexes remain readable.
- Automatic reading start dates now require two active minutes plus forward progress, or five active minutes on one
  long page, so briefly previewing a book does not start its reading timeline.

### Fixed

- Crossing the real EPUB or XTC ending now completes the book automatically. Leaving from its final page or a rounded
  100% asks for confirmation instead, including when reading-stat tracking is disabled; declining suppresses the
  prompt until reading advances.
- The first completion date is preserved when a finished book is reopened, reread, or marked finished again, and a
  pending Reading Achievement screen survives reader replacement or restart.
- Opening Reading Stats reconciles a completed book's persistent start and finish dates with Finished Books without
  requiring a fake edit, while merging legacy entries accidentally keyed by the cache path.
- Per-book statistics migrate compatibly from schema versions 4 and 5 to version 6.

## [v1.4.0-yacp.155] - 2026-07-31

### Added

- X3 readers can use the OEM black-and-white reinforcement waveform for periodic screen maintenance without a
  full-screen flash. It is also available as an assignable manual action.
- Newly completed EPUB and XTC books now open a Reading Achievement screen with reading time, sessions, average session,
  favorite reading period, reading span, completed-book count, and total device reading time.
- Reading Stats now includes a persistent Finished Books view containing the 32 most recently completed books, with
  five-entry pagination and total device reading time.

### Changed

- Periodic screen maintenance now supports intervals of 1, 5, 10, 15, 30, or 60 pages, or Never. Required cleanup
  after images, grayscale content, popups, wake, and sleep remains a full refresh.
- The firmware profile chooser appears once per firmware version. Fresh installations recommend the YACP profile;
  upgrades safely default to preserving the current configuration.
- The recommended YACP profile uses Left, Right, Confirm, Back, short-Power sleep, long-Power full refresh, Quick
  Resume after five minutes, no-flash X3 reinforcement on every page, and text antialiasing off.
- Home performs one full refresh on entry and after closing its built-in menu, then returns to fast updates.
- Release firmware names now follow `YACP-<version>-tiny.bin` and `YACP-<version>-xlarge.bin`.

### Fixed

- Finished Books is reachable on X4 and other devices without a real-time clock, and every retained entry is
  navigable instead of only the first five.
- Moving a completed EPUB to `/Read/` now migrates its path identity explicitly, so distinct books with the same
  title no longer overwrite one another in Finished Books.
- All 26 languages now provide their own label for the 60-page maintenance interval.
- Finished-books index failures close the SD file before cleanup or backup recovery.
- Host unit tests now register their PlatformIO target and use compiler-appropriate warning flags on Windows.
- Release builds no longer retain a variable used only by disabled debug logging in the SD font registry cleanup.

## [v1.4.0-yacp.154] - 2026-07-30

### Changed

- Validated the complete pre-release change set on both Tiny and XLarge firmware targets.

## [v1.4.0-yacp.153] - 2026-07-30

### Changed

- Retried the combined release build with an explicit UTF-8 host environment after Windows CP1252 rejected the
  translation report.

## [v1.4.0-yacp.152] - 2026-07-30

### Changed

- Validated the complete pre-release change set on the Tiny firmware target.

## [v1.4.0-yacp.151] - 2026-07-30

### Fixed

- Finished-books index failures now always close the SD file before cleanup or recovery.

## [v1.4.0-yacp.150] - 2026-07-30

### Changed

- Re-ran Tiny validation with UTF-8 host output after Windows CP1252 rejected the translation report.

## [v1.4.0-yacp.149] - 2026-07-30

### Changed

- Re-ran Tiny validation in deterministic single-job mode after the Windows host stalled in parallel SCons.

## [v1.4.0-yacp.148] - 2026-07-30

### Changed

- Re-ran Tiny validation after the previous host terminal interrupted PlatformIO output.

## [v1.4.0-yacp.147] - 2026-07-30

### Changed

- Validated the Home refresh and persistent Finished Books page on the Tiny firmware target.

## [v1.4.0-yacp.146] - 2026-07-30

### Changed

- Validated the Home refresh and persistent Finished Books page in the X3 simulator.

## [v1.4.0-yacp.145] - 2026-07-30

### Added

- Reading Stats now includes a persistent Finished Books page after Reading Rhythm, including an empty state and
  this device's total reading time.
- A bounded finished-books index is updated on completion and reader exit without scanning the full library.

### Fixed

- Home now performs one full refresh when it is entered and when the built-in YACP menu closes.

## [v1.4.0-yacp.144] - 2026-07-30

### Changed

- Validated short-Power firmware-profile confirmation on the Tiny firmware target.

## [v1.4.0-yacp.143] - 2026-07-30

### Fixed

- A short Power press now confirms the firmware profile even when the previous profile assigned that press to sleep.

## [v1.4.0-yacp.142] - 2026-07-30

### Changed

- Validated the device-wide reading-time card on the Tiny firmware target.

## [v1.4.0-yacp.141] - 2026-07-30

### Changed

- Validated the device-wide reading-time card on the X3 simulator.

## [v1.4.0-yacp.140] - 2026-07-30

### Added

- The book-completion achievement now shows total reading time accumulated on this device, including the session
  being completed.

## [v1.4.0-yacp.139] - 2026-07-30

### Changed

- Validated the stable firmware-profile chooser and empty Reading Stats entry on the simulator and Tiny target.

## [v1.4.0-yacp.138] - 2026-07-30

### Changed

- The firmware-profile panel now always presents the same YACP, CrossInk, and keep-current choices, defaults safely
  to keeping current settings on upgrades, and shows the complete effect of the selected profile.
- Firmware-profile labels are translated directly in all 26 supported languages.
- Reading Stats now remains available from every Home menu before any book, session, or completion has been recorded.

## [v1.4.0-yacp.137] - 2026-07-30

### Changed

- Validated version-aware setup-panel persistence and legacy-flag migration on the simulator and Tiny target.

## [v1.4.0-yacp.136] - 2026-07-30

### Changed

- A genuinely fresh installation still recommends the YACP preset, while upgrades preselect an already matching
  YACP, CrossInk, or Custom configuration.

## [v1.4.0-yacp.135] - 2026-07-30

### Changed

- The YACP/CrossInk setup panel now appears once for every newly flashed firmware version, while subsequent boots of
  that same version continue normally.
- On firmware upgrades, the panel initially selects the currently matching YACP, CrossInk, or Custom configuration.

## [v1.4.0-yacp.134] - 2026-07-30

### Changed

- Validated the revised first-run YACP preset and Settings-to-Home refresh on the Tiny firmware target.

## [v1.4.0-yacp.133] - 2026-07-30

### Fixed

- The translated first-run setup description now wraps within the narrow display instead of drawing past its edges.

## [v1.4.0-yacp.132] - 2026-07-30

### Changed

- Revalidated the first-run YACP preset and Settings-to-Home refresh in the Linux simulator environment.

## [v1.4.0-yacp.131] - 2026-07-30

### Changed

- Returning from Settings now gives every Home theme one clean full refresh.
- The recommended first-run YACP preset now enables Quick Resume after a five-minute idle timeout, uses X3 no-flash
  screen reinforcement on every page, disables text anti-aliasing, and maps the front buttons as Left, Right, OK,
  Back.

## [v1.4.0-yacp.130] - 2026-07-30

### Changed

- Validated the current completion-achievement statistics and English simulator reference capture together.

## [v1.4.0-yacp.129] - 2026-07-30

### Changed

- Updated simulator QA coverage for the current completion-achievement statistics.

## [v1.4.0-yacp.128] - 2026-07-30

### Changed

- Repository documentation and QA artifacts now reflect only the current Reading Stats feature set.

## [v1.4.0-yacp.127] - 2026-07-30

### Changed

- Integrated the completion-achievement screen with the latest screen-maintenance build.

## [v1.4.0-yacp.126] - 2026-07-30

### Changed

- Screen-maintenance settings and the assignable no-flash refresh action now use shorter, more explicit labels
  distinguishing the flashing full refresh from the X3 no-flash refresh, translated in all 26 firmware languages.

## [v1.4.0-yacp.125] - 2026-07-30

### Changed

- The completion achievement shows the book's reading span in days.

## [v1.4.0-yacp.123] - 2026-07-30

### Added

- Leaving a newly finished EPUB or XTC book for Home now shows a celebratory achievement screen with the book's
  reading time, sessions, average session, favorite reading period, reading span, and completed-book count.
- Completion-achievement labels are translated in all 26 firmware languages.

### Fixed

- Automated Reading Stats demo captures now honor the simulator's English screenshot override.

## [v1.4.0-yacp.111] - 2026-07-30

### Added

- Periodic screen maintenance can now use either a full refresh or the X3 OEM black-and-white reinforcement waveform
  at the selected page interval. Manual refreshes and cleanup required after images or popups remain full.
- The same no-flash X3 black-and-white reinforcement is available as an assignable short/long Power action and as a
  long-press Confirm or Back action; grayscale pages and non-X3 devices retain the full-refresh fallback.

## [v1.4.0-yacp.97] - 2026-07-30

### Changed

- Reader full-refresh frequency now defaults to 30 pages and offers 60 pages or Never; Never keeps required cleanup
  refreshes after images, indexing, wake, and sleep screens.
- Choosing the recommended YACP button experience during first-run setup now makes a short Power press sleep and a
  long Power press refresh the screen, while the CrossInk choice retains its original Power behavior.

## [v1.4.0-yacp.93] - 2026-07-28

### Changed

- The centered YACP mark on the current-book Home card is now pale gray, using a sparse e-ink dither that remains
  visible on its light-gray background without a duplicated bitmap.
- Project documentation now demonstrates the YACP Home recent-book switcher and groups Reading Stats, Reading Rhythm,
  and Autonomy with dedicated grayscale media previews that remain clear when resized.

## [v1.4.0-yacp.92] - 2026-07-28

### Changed

- The README now groups the combined Reading Stats summary and Reading Rhythm together, with Autonomy documented
  separately.
- The README now demonstrates the custom YACP Home as an animated Left/Right recent-book switcher using the default
  YACP physical button order, with visible progress for both example books.
- Documentation screenshots now use dedicated media previews with solid grayscale fills, avoiding moiré when GitHub
  or social platforms resize the original 1-bit e-ink captures.

## [v1.4.0-yacp.91] - 2026-07-28

### Added

- An English simulator screenshot documenting YACP's combined first Reading Stats view.

### Changed

- The pale-gray YACP Home mark is slightly darker for better legibility while remaining lighter than its card.

## [v1.4.0-yacp.90] - 2026-07-28

### Changed

- The centered YACP Home mark is now pale gray instead of white, using a sparse e-ink dither that remains visible on
  the light-gray card without a duplicated bitmap.
- The deterministic YACP Home screenshot now uses English UI and English Alice in Wonderland metadata.

## [v1.4.0-yacp.89] - 2026-07-28

### Added

- A deterministic X4 simulator screenshot of the YACP Home with Alice in Wonderland as the current book.

### Changed

- The centered YACP mark on the current-book Home card is now white, while the boot, sleep, documentation, and web
  variants remain black.

## [v1.4.0-yacp.88] - 2026-07-28

### Added

- First downloadable YACP release for the `tiny` and `xlarge` font variants.
- A rounded, cover-free YACP Home focused on the current book, direct resume, progress, reading time, and estimated
  time left.
- Autonomy tracking with a coarse battery-versus-active-use graph and no periodic wake-up.
- A first-start button-layout choice for YACP, CrossInk, or an existing custom mapping.
- Reading Rhythm with daily intensity, weekly reading time, reading days, and current and best streaks.

### Changed

- Clean profiles default to the YACP Home, Quick Resume after automatic sleep, and text antialiasing off.
- The X3 quiet reader path uses the lower CPU frequency, rate-limited USB checks, prompt button-edge confirmation,
  and debounced progress writes.
- Built-in fonts no longer trigger SD-card font discovery or retain its catalogue. SD fonts remain available after
  explicit selection or font management.
- Sequential chapter preparation covers one-page chapters and direct last-page jumps while respecting the existing
  memory guard and avoiding repeated SD probes.
- The selected rounded reading-path mark is shared by boot, sleep, Home, the web portal, and documentation.
- Documentation now presents the project direction, YACP theme choices, release variants, and generated simulator
  examples in a shorter structure.

### Fixed

- X3 Quick Resume no longer performs the full-screen black synchronization pass when sleeping or restoring a cached
  reader page.
- Home and Reading Stats navigation now follows the displayed logical-button actions.
- EPUB grayscale rendering reuses one bounded strip buffer instead of allocating it again for each page.

### Removed

- The unsupported X3 battery charge-limit experiment and hardware diagnostic are not included in this release.

## [v1.4.0-yacp.87] - 2026-07-28

### Removed

- Removed the unsupported X3 battery charge-limit experiment and its hardware diagnostic from the firmware and
  Device settings.

## [v1.4.0-yacp.86] - 2026-07-28

### Changed

- Recalibrated the battery tracking demonstration to 29 hours and 47 minutes of active reading across 10 days,
  with 35 percent remaining.

## [v1.4.0-yacp.85] - 2026-07-28

### Fixed

- The deterministic battery tracking demo now reports the intended 10-day span in its summary.

## [v1.4.0-yacp.84] - 2026-07-28

### Changed

- Rebuilt the Tiny charge-control diagnostic after a concurrent simulator build regenerated the shared i18n headers
  during the first compilation attempt.

## [v1.4.0-yacp.83] - 2026-07-28

### Changed

- The deterministic battery tracking demo now shows 29 hours and 47 minutes of reading across 10 days with 35 percent remaining,
  replacing the overly optimistic previous curve.

## [v1.4.0-yacp.82] - 2026-07-28

### Added

- The X3 Device settings now show a compact, read-only BQ27220 charge-control diagnostic containing the security
  transitions and raw Operation Config A/PFC/BAT_GD values.

### Changed

- Charge-limit application is disabled in this diagnostic Tiny build so inspecting the BQ27220 cannot modify its
  charge-limit Data Memory parameters.

## [v1.4.0-yacp.81] - 2026-07-28

### Changed

- Rebuilt the Tiny firmware after isolating the simulator-only charge-limit compatibility guard.

## [v1.4.0-yacp.80] - 2026-07-28

### Changed

- Rebuilt the X3 simulator in an isolated PlatformIO build directory for the battery tracking documentation capture.

## [v1.4.0-yacp.79] - 2026-07-28

### Changed

- Rebuilt the Tiny firmware containing the X3 BQ27220 unseal-key correction with UTF-8 build output after the
  Windows console rejected translated language names.

## [v1.4.0-yacp.78] - 2026-07-28

### Fixed

- Simulator builds now exclude the hardware-only X3 battery charge-limit setting and its apply callbacks.

## [v1.4.0-yacp.77] - 2026-07-28

### Changed

- Rebuilt the Tiny firmware containing the X3 BQ27220 unseal-key correction after the local PlatformIO launcher
  retry.

## [v1.4.0-yacp.76] - 2026-07-28

### Fixed

- X3 charge-limit detection now uses the two BQ27220 unseal words programmed by the stock Xteink firmware, allowing
  the capability probe to reach Full Access instead of hiding the setting after an unsuccessful default-key attempt.

## [v1.4.0-yacp.75] - 2026-07-28

### Changed

- README battery tracking documentation now includes a deterministic X3 simulator capture.

## [v1.4.0-yacp.73] - 2026-07-28

### Fixed

- X3 charge-limit detection now enters BQ27220 Full Access after unsealing, as required before reading or updating
  Data Memory, instead of hiding the setting when the gauge remained in Unsealed Access.

## [v1.4.0-yacp.72] - 2026-07-28

### Fixed

- X3 charge-limit detection now decodes the BQ27220 `SEC1` and `SEC0` status bits from their documented positions,
  allowing sealed gauges to be unsealed temporarily for the capability probe instead of hiding the setting.

## [v1.4.0-yacp.71] - 2026-07-28

### Changed

- The initial button-layout screen now detects mappings that match neither YACP nor CrossInk and offers a
  preselected option to preserve the user's current custom layout.

## [v1.4.0-yacp.70] - 2026-07-28

### Changed

- Rebuilt the Tiny development firmware for deployment to the local test reader.

## [v1.4.0-yacp.69] - 2026-07-28

### Changed

- The initial button-layout choice now has one production behavior for every device: ask once, persist the answer,
  and do not include a development-only override.

## [v1.4.0-yacp.68] - 2026-07-28

### Changed

- Rebuilt the Tiny development firmware with the selected rounded YACP icon after the local PlatformIO validation
  retry.

## [v1.4.0-yacp.67] - 2026-07-28

### Changed

- Replaced YACP's angular reading-path mark with the selected rounded four-line icon everywhere the identity is used.

## [v1.4.0-yacp.66] - 2026-07-28

### Changed

- Rebuilt the Tiny development firmware with UTF-8 build output after the Windows console rejected translated
  language names.

## [v1.4.0-yacp.65] - 2026-07-28

### Changed

- Rebuilt the Tiny development firmware after the first-run button-layout implementation.

## [v1.4.0-yacp.64] - 2026-07-28

### Added

- First startup now asks users to choose between the recommended YACP front-button order
  (Left, Right, Confirm, Back) and the familiar CrossInk order, then remembers the choice.

## [v1.4.0-yacp.63] - 2026-07-28

### Changed

- Rebuilt the Tiny development firmware for on-device verification of the one-time YACP Home refresh and centered
  logo treatment.

## [v1.4.0-yacp.62] - 2026-07-28

### Changed

- Boot, sleep, and YACP Home now share one inline monochrome logo constant instead of emitting a separate flash copy
  in each translation unit.

## [v1.4.0-yacp.61] - 2026-07-28

### Changed

- The first render of each YACP Home load now uses one full e-ink refresh to clear text ghosting from its gray book
  surface; subsequent book navigation and secondary-screen returns keep fast refreshes.
- The embedded monochrome YACP mark is now centered on the gray book surface without adding an SD read or runtime
  allocation.

## [v1.4.0-yacp.60] - 2026-07-28

### Changed

- The YACP Home lateral buttons now select the previous or next valid book in most-recent-first opening order.
  Unavailable boundary actions are blank and do nothing; Recent Books and Reading Stats remain available in Menu.

## [v1.4.0-yacp.59] - 2026-07-28

### Changed

- Reading Stats now uses Back to return Home and Enter to edit the current book's dates, matching the displayed
  button hints and avoiding the previous duplicate Home actions.

## [v1.4.0-yacp.56] - 2026-07-28

### Changed

- Rebuilt the Tiny development firmware for on-device verification of the experimental X3 charge-limit setting.

## [v1.4.0-yacp.55] - 2026-07-28

### Fixed

- The YACP Home now stays inside the physical-button safe area in every orientation and uses fonts available in the
  Tiny firmware variant.
- Entering or returning to the YACP Home no longer causes a redundant display refresh or probes cover and optional
  menu data that the reading-focused screen does not show.

## [v1.4.0-yacp.54] - 2026-07-28

### Added

- X3 Device settings can now request an experimental 80%, 90%, or 100% charge limit when the BQ27220 reports that
  its existing BAT_GD output is already configured to control the charger.

## [v1.4.0-yacp.47] - 2026-07-28

### Changed

- The default Home theme is now presented as YACP: a RoundedRaff-inspired typographic card focused on resuming the
  current book, with only progress, reading time, and estimated time left on the main screen.
- Home actions now map directly to Menu, Read, Recent Books, and Reading Stats; the secondary menu keeps browsing,
  autonomy, saved items, OPDS, transfer, and settings available on demand.

### Removed

- The default YACP Home no longer loads or generates a cover thumbnail, stores a cover snapshot in heap, opens the
  EPUB solely for its chapter title, or probes optional Home menu data before the menu is requested.

## [v1.4.0-yacp.43] - 2026-07-28

### Added

- An Autonomy screen with a coarse battery-versus-active-use graph, days and active time since charging, using only
  sleep-transition samples and the existing state save, plus deterministic simulator demo data.
- Dashboard UI theme for the Home screen, showing the current book cover and reading stats.
- Nearby Position Sync for sending or applying the current EPUB position between two CrossInk devices over ESP-NOW.
- Web EPUB optimizer support for CrossInk location metadata, so optimized EPUBs can keep better progress and stable page numbers.
- Reading Stats support for XTC and XTCH books, including reader menus, Home and sleep screen stats, mark finished, delete stats, and preserving stats when clearing book caches.
- Reading Rhythm panel in Reading Stats, showing a four-level daily heatmap, an aligned weekly reading-time chart, and
  reading-day consistency over the last 12 months, including the current month.
- Web file manager image previews, so PNG, JPEG, BMP, GIF, and WebP files can be viewed inline before downloading.
- X3 simulator environment and deterministic Reading Rhythm demonstration data for repeatable screenshots.
- Personal project documentation covering YACP's lineage, priorities, current work, validation plan, and closed
  contribution policy.

### Changed

- YACP now uses its continuous reading-path logo on the boot and default sleep screens, web portal, README, and
  documentation home page.
- Automated simulator screenshots now always render the English UI for consistent GitHub documentation, without
  changing the language saved in the simulator profile.
- French translations now cover every active firmware string; inactive keys remain stripped and other languages keep
  the established English fallback.
- Clean profiles now default to Dashboard, text antialiasing off, and Quick Resume after automatic sleep while
  preserving valid choices already saved by existing users.
- Dashboard and Minimal Home now use remapped logical buttons. YACP uses Confirm to resume the current book and Left
  for the explicit Recent Books screen, while Minimal keeps its established Menu, Browse, Settings, and Read actions.
- SD-card fonts are now discovered and loaded only after an SD family is selected or a font selector/manager is
  opened, including lazy loading in the web settings page; built-in fonts no longer scan font directories or retain
  the SD font catalogue, and transient load failures no longer erase the selected SD family.
- Dashboard and Minimal now cache only the rendered cover region instead of their full Home tile, reducing the X3
  Dashboard snapshot from about 40.4 KiB to about 16.5 KiB.
- Sequential next-chapter preparation now also covers one-page chapters and direct jumps to the last page while
  retaining the existing low-memory guard and avoiding repeated SD probes once that next chapter is prepared.
- The Quick Resume documentation now links the X3 before / after recording submitted with CrossPoint PR #2698.
- Repository governance, scope, and inherited development notes now state that YACP has no Issues, Discussions, pull
  requests, support, or contact channel.
- X3 reader button edges now receive a prompt confirmation sample during the 50 ms low-power polling cadence,
  reducing perceived page-button latency without continuously increasing the polling rate.
- EPUB grayscale rendering now reuses one bounded strip buffer per loaded section and releases it before indexing,
  avoiding repeated per-page heap allocation while preserving memory for chapter construction.
- X3 book readers now lower the CPU clock promptly between unchanged 50 ms button polls, reduce fuel-gauge USB checks
  while idle, lower the CPU clock while the e-ink controller is busy refreshing, and debounce progress writes while
  still flushing the latest position on normal reader exit.
- This local custom build is branded YACP (Yet Another CrossPoint) while retaining CrossInk's storage formats and
  upstream-compatible architecture.
- Large EPUBs, SD-card font-heavy books, and cover thumbnails now open, index, and generate more reliably under low-memory conditions.
- Home and sleep screens now load more cover and thumbnail data only when needed, reducing reader startup work and reusing cached cover data where possible.
- Built-in reader font choices have been reduced to Lexend Deca and Bitter, reducing firmware size while keeping fallback glyph coverage.
- Book and all-books reading summaries now share one concise page, while long-term trends remain on the dedicated
  Reading Rhythm page.
- Reading Rhythm now shows a dash instead of zero for months without recorded reading days.
- Main branch pushes now build both firmware variants, report a release checklist, and prepare a draft release with
  descriptive YACP artifact names and SHA-256 checksums.

### Removed

- Teensy firmware builds are no longer produced for releases or release candidates.

### Fixed

- Reading Rhythm now returns to the Reading Stats summary with the previous-page button, while Back goes Home; the
  Reading Stats and Autonomy Home menu entries also use distinct icons.
- Clean firmware builds now expose the inflater dependency required by compressed built-in fonts to PlatformIO.
- Home menu navigation now derives its bounds from the rendered entries, so Settings remains selectable after adding
  the Autonomy screen.
- X3 reader idle power saving keeps the CPU awake at 10 MHz instead of entering ESP light sleep, which can drop the
  board's power latch and prevent page buttons from waking the device.
- EPUB reading now silently pre-indexes the next chapter on the penultimate page, avoiding the chapter-entry indexing
  pause introduced in CrossInk 1.4.0.
- X3 Quick Resume no longer performs a full-screen black refresh when entering sleep or restoring a cached reader
  frame.
- EPUB render-mode and Safe Mode toast messages now clear reliably, even when the reader is low on memory.
- EPUB Reading Stats no longer drops unsaved page-turn counts after viewing the stats screen mid-session.
- KOSync is more reliable with many SD-card fonts installed, reducing low-memory failures during secure sync requests and uploads.
- Web file manager actions now handle filenames with special characters safely and reject unsafe rename characters before saving.
- Auto Turn interval settings and related action prompts opened from long-press shortcuts now stay open after releasing the shortcut button.
- EPUB footnote previews no longer show clipped status-bar labels or misleading reader progress indicators, and clipping selection now works from footnote previews.
- Font selection no longer reopens the font preview after choosing a font.
- EPUB chapters with stale publisher style data now rebuild it instead of opening without the book's styling.
- Large SD-card font EPUBs no longer overlap characters after font or line-spacing changes, and clipping selection can fall back to a built-in UI font when needed.
- EPUB cover and thumbnail generation is more reliable with custom SD-card fonts selected and optimized books under low-memory conditions.
- Web EPUB optimizer now preserves more PNG and SVG artwork on-device, including transparent PNGs, dividers, and images in malformed or XML-declared chapters.
- Unsupported SVG images in EPUB chapters are now skipped silently instead of triggering low-memory image warnings.
- Nearby Position Sync now silently restarts back into the reader after using ESP-NOW, matching other WiFi sync flows and reducing post-sync memory fragmentation.
- EPUB page cache loading now uses fewer small heap allocations, reducing fragmentation-related reader failures.
- EPUB grayscale page turns on X3 now use the grayscale-aware display base, reducing the moment where new text appears too dark before the anti-aliased overlay finishes.
- EPUB chapters with many inline anchors, footnote links, malformed XHTML, large publisher styles, or SD-card fonts are less likely to fail or get stuck on the indexing screen.
- EPUB opening and image rendering now recover from more low-memory conditions instead of rebooting, including landscape image pages and books that need lighter render modes.
- EPUB clipping selection now follows right-to-left line order when selecting Hebrew and other RTL text.
- Lyra Carousel no longer shows a blank carousel after returning from WiFi-related File Transfer screens and moving between the menu row and book row.
- Generated SD-card font packages now include the same core glyph coverage as built-in reader fonts.
- Manage Fonts no longer crashes while loading or reloading large SD-card font lists.
- Minimal Home no longer swaps to another recent book when returning from Settings when Back button is mapped to the first button.
- Cancelling a font download now stops on the first Cancel button press instead of needing several presses.
- The `Inverted` sleep cover filter now keeps book covers unchanged on Minimal and Dashboard sleep screens while switching the background to white.
- Rare EPUB open or thumbnail crashes during ZIP decompression are fixed.
- The current CrossPoint simulator now builds against the firmware WiFi disconnect path.

## [v1.3.4] - 2026-06-24

### Added

- File Browser now indexes large SD-card folders so directories with many books can be browsed without loading every filename into memory at once.
- EPUB text clipping with saved highlights, clipping lists, and Kindle-style `/My Clippings.txt` export.
- `Create Clipping` is now available as a reader shortcut for short/long Power, long-press Menu, and long-press Back actions.
- Per-book EPUB options for font, layout, styling, reading aids, and render modes, including `CrossInk Default`, `Balanced`, and `Light` modes for difficult books.
- Arena allocator (`lib/Memory/Arena.h`) for burst-then-discard allocation patterns - reduces heap fragmentation during EPUB parsing and page layout over long reading sessions.
- Optimized EPUBs now store location metadata at `META-INF/x-locations.json`.
- X3 SD-card writes now use the RTC for file timestamps when the clock is available.

### Changed

- The EPUB reader menu now splits the growing menu into 3 screens, labels per-book settings as `Book Options`, and avoids showing duplicate `Orientation` controls.
- The `Inverted` sleep cover filter now flips Minimal and Reading Stats sleep screens to black text on a white background.

### Fixed

- Calibre Wireless transfer status no longer stacks the last received-file message on top of the upload percentage.
- X3 Tilt Direction now labels left/right choices as `Left-Right` and `Right-Left`, with existing left/right preferences migrated to keep the same physical tilt behavior.
- EPUB layout now honors publisher page-break CSS, avoids stretching justified spaces before closing punctuation, and keeps large CSS rule sets in a smaller disk-backed lookup cache.
- EPUB first-open conversion now uses more compact OPF manifest lookups and streams cover-wrapper parsing to avoid large temporary heap buffers on books with huge manifests.
- EPUB chapters that run out of memory now retry with `Balanced`, `Light`, and final `Safe Mode` rendering before showing an error, apply the same fallbacks during next-chapter pre-indexing, and let book action menus reset a book's reader settings if Safe Mode still cannot open it.
- EPUB reader font-size changes now restore the current chapter position by content instead of jumping far backward after re-indexing.
- Reading Stats now use the reader's last live book time-left estimate instead of showing a separate fallback estimate.
- Per-book reading stats now migrate compatible legacy `stats.bin` files into the `stats_v5.bin` flow instead of resetting when only the old filename exists.
- Lyra Carousel Home menu rendering now avoids extra label allocations that could crash builds under low memory.
- Lyra Carousel Home cover refresh no longer risks a reboot when memory is tight after returning to or selecting a recent book.
- EPUB image-heavy chapters no longer risk a reboot while saving their reading cache under low memory.
- TXT readers now stay open when pressing a page-turn button at the end of the file.
- Long-press reader shortcuts that open another screen no longer close or confirm it again when releasing the shortcut button.
- RoundedRaff's header battery icon and percentage now sit lower to avoid clipping at the top edge.
- Lyra Carousel now keeps the Home header current when rendering the menu or restoring cached carousel frames, preventing stale battery and clock values while navigating between books.
- Web file manager multi-delete now handles larger selections without failing after a small batch.
- Portuguese EPUBs now use Portuguese hyphenation rules instead of leaving long words unhyphenated when Hyphenation is enabled.
- Progressive JPEG EPUB covers now render more smoothly in generated cover and thumbnail BMP assets.
- EPUB section layout now flushes long text runs earlier when Bionic Reading or Guide Dots are enabled, reducing low-memory failures on difficult books.
- Footnotes in EPUBs with very large shared notes sections no longer cause long stalls when opened.
- Firmware updates now follow GitHub asset redirects before streaming the install.
- Tiled grayscale rendering now serializes display transfers on the shared SPI bus to avoid display glitches during SD activity.

## [v1.3.3] - 2026-06-13

### Added

- `File Browser Display` in `Settings > System > Files & Cache` for choosing one-line or two-line file browser rows across all themes, while preserving Minimal users' existing two-line display on upgrade.
- `Hide File Extension` in `Settings > System > Files & Cache` for expanding file-browser filenames by hiding the right-side extension label.
- Device Name in Settings > System > Device for customizing the KOReader Sync and Nearby Stats Sync device label.
- Additional shortcut options and new ability to add custom shortcuts for Long-press Back Action.
- Delete Reading Stats actions in the EPUB reader and book action menus for clearing one book's stats without deleting its cache.

### Changed

- CrossInk settings now save to `/.crosspoint/crossink-settings.json`, with a one-time fallback migration from `/.crosspoint/settings.json`, so switching between firmware builds is less likely to reset preferences.
- The X3 clock visibility setting is now phrased as `Hide Clock`, with existing `Show Clock` preferences migrated to the matching hide behavior.

### Fixed

- RoundedRaff's date shown in settings now sits lower on X3 devices instead of overlapping the battery.
- Clear Bookmark List now asks for confirmation before deleting a book's bookmarks.
- Clear Reading Cache now preserves per-book reading stats while continuing to leave all-time reading stats untouched.
- Moving finished EPUBs to `/Read` now consistently preserves reading progress, per-book stats, bookmarks, and resume state.
- Book settings option lists now return to the submenu they were opened from when pressing Back.
- Lyra Carousel now refreshes its cached Home icon row after OPDS, Reading Stats, or Bookmarks icons appear or disappear.
- KOReader Sync failure screens now wrap long error messages and shut down WiFi cleanly before returning to the book.
- Sleep Screen > Cover now generates the current book cover on demand instead of falling back to the dark sleep screen when the setting is changed after opening a book.
- File Browser now previews PNG images instead of trying to open them as EPUBs, and hides common macOS and Windows metadata files.
- File Browser now refreshes immediately after falling back to the root folder from a stale saved path.
- File Browser now stops loading oversized folders before low memory can crash the device and shows a memory error instead.
- TXT reader long-press Power page turns now work when Long Power Button is set to Page Turn.
- SD-card font read failures no longer risk a reboot while cleaning up the failed file read.
- Page Overlay sleep screens no longer force EPUB chapters to re-index after waking.
- Page Overlay sleep screens now use the current screen as the overlay background outside the reader instead of trying to rebuild a stale book page.

## [v1.3.2] - 2026-06-10

### Added

- Current date in the top-right Settings header on X3 devices.
- Dark Reader Mode for EPUB and TXT reading screens, plus shortcut actions for the power button and front-button long press.
- File Browser long-press folder action for choosing a custom sleep-image folder instead of only `/.sleep` or `/sleep`.
- Expanded X3 Reading Stats, including streaks, time charts, editable dates, all-time backups, reset controls, an idle-time threshold, and the `Minimal Stats` sleep screen.
- `Reset Reading Pace` in the EPUB reader menu when Time Left is enabled, for clearing only the time-left pace estimate while keeping book reading stats.

### Changed

- Display, Reader, and Controls settings now open list menus instead of cycling through options one by one.
- Reading time and time-left pace tracking now ignore page intervals longer than the configured idle-time threshold.
- Web portal pages now use shared templates, stylesheet, and logo assets, reducing on-device page size and improving browser caching.
- Already-cached EPUBs now open directly to the first page without an extra book-loading popup refresh.
- Reader font-size choices now show point sizes like `10 pt` instead of names like `Tiny`.

### Fixed

- Inverted reader menus now honor orientation-aware side-button navigation.
- EPUB book time-left estimates now wait for more session pace samples and use a progress-based floor after pace data exists, reducing swings from unusually short or long pages.
- Deleting an EPUB book cache now preserves that book's reading stats and pace data.
- X3 clock settings now have clearer UTC offset editing, and `Sync Date/Time` can use saved WiFi networks automatically.
- Home, Lyra Carousel, WiFi setup, and SD-card font flows now release memory more aggressively to avoid freezes or crashes on constrained builds.
- Vietnamese settings labels no longer show replacement diamonds after generated translation offsets shifted.
- KOReader Sync now lands correctly at chapter starts and shows more specific connection guidance.
- EPUB bookmarks saved under the old unstable path hash now show up again, including for books moved to `/Read`.
- SD-card font downloads now use versioned direct S3-hosted HTTP endpoints with CRC validation, avoiding GitHub release redirects and ESP32-C3 TLS stalls when loading the font catalog.
- EPUB text blocks now keep the book's alignment style when an inline image appears before the text.

## [v1.3.1] - 2026-05-28

### Added

- EPUB reading-position improvements, including bookmark anchors, bookmark preview snippets, and optional chapter/book time-left estimates.
- Nearby Reading Stats sync with separate totals for this device and all synced CrossInk readers.
- Per-server OPDS filename settings so downloaded books can use either Author - Title or Title - Author.
- EPUB render heap diagnostics that include the largest allocatable block, not just total free heap.

### Changed

- Moved the X3 reader clock into a new top-centered status bar and moved clock settings to Settings > System > Device.
- Reworked Display, Reader, Controls, in-reader options, and larger System settings groups so related options open as submenus.
- Improved OPDS and font download responsiveness by reducing progress-update overhead and temporarily disabling WiFi power saving during transfers.
- Book selection now shows a loading popup before EPUB indexing or cache loading begins.
- Delayed the automatic finished-book prompt until the reader leaves the chapter where they reach 99%.

### Fixed

- WiFi settings screen now keeps the displayed MAC address consistent with the router-visible WiFi address.
- Reader UI issues with inverted menu button hints, Lyra Carousel popups, and Auto Page Turn interval persistence.
- Web uploads and KOReader Sync progress saves now preserve progress, stats, settings, and valid resume data for refreshed book files.
- OPDS low-memory handling now shows a specific parser-buffer memory message and releases SD-card fonts before catalog loading.
- EPUB cache, CSS, table, SD-card font, and allocation failure paths now recover, retry, or stop cleanly under low memory instead of opening unstyled pages, failing unnecessarily, or risking a reboot.
- EPUB text with invisible word-joiner characters no longer shows replacement diamonds for missing font glyphs.
- Clarified the low-memory EPUB image warning so it says some or all images may be missing.

## [v1.3.0] - 2026-05-21

### Added

- Back/Cancel support while downloading books from OPDS catalogs.
- Recent Books long-press menu in both List and Grid views with delete, cache delete, completion, and remove-from-recents actions.
- Minimal sleep screen option that shows the current book cover and reading progress on a dark background.
- More detailed WiFi connection debug logs for scans, selected networks, status changes, disconnect reasons, and timeouts.
- 9pt `Itty Bitty` reader font size, plus build flags for omitting Itty Bitty and Large reader font assets in size-constrained firmware variants.
- In-reader confirmation message when a shortcut turns tilt-to-turn on or off.

### Fixed

- WiFi and OPDS connection-flow edge cases: manual Settings connections now show the connected status before continuing, copied or corrupted saved-password files are rejected before use, OPDS retries show loading before requests, and large OPDS feeds fail safely under low memory instead of rebooting.
- Reader and Home UI polish issues, including landscape status-bar settings, missing Vietnamese labels, File Browser and Lyra Carousel icon alignment, cover thumbnail artifacts, and duplicate Home progress/stat loading.
- EPUB cache and low-memory handling now use stable cache folder keys, migrate older cache folders where possible, rebuild stale section caches, lay out very long text blocks earlier, stream table fallback content when heap is tight, and clarify the warning text.
- Sleep-entry, network, and SD-card font download reliability improvements: cached sleep-screen assets are reused, OPDS pages idle normally after load, the X3 tilt sensor sleeps outside the reader, WiFi power saving is disabled during transfers, WebDAV stack usage is lower, longer stalls are tolerated, interrupted font files are retried, and active reader fonts are freed when needed.
- Remaining reader service edge cases, including an XTC chapter selector crash on memory-constrained builds, SD-card font size selection, SD-card font-size shortcuts skipping manually installed sizes, and KOReader Sync login compatibility with self-hosted servers that return valid JSON on success.

### Changed

- Modified upstream "page-as-sleep" behavior into a new `Sleep Screen > Quick Resume` option, which also keeps `Quick Resume on Timeout` on, and renamed the timeout-only toggle.
- Improved reader and browser menu behavior by moving the Footnotes shortcut above Select Chapter, wrapping long book titles in action menus, and reducing progress-screen repaint work during OPDS and SD font downloads.

## [v1.2.11.1] - 2026-05-15

### Changed

- Removed Medium font size from `xlarge` build to get it below the size limit

### Fixed

- Lyra Carousel is now included by activating the build flag `DCROSSINK_ENABLE_LYRA_CAROUSEL=1`

---

## [v1.2.11] - 2026-05-14

### Added

- New personal theme: "Minimal"
- Custom sleep timer picker so `Time to Sleep` can be set from 1 to 30 minutes instead of cycling fixed presets.
- In-reader Controls shortcut for customizing buttons without leaving the book.
- Bookmark cleanup shortcuts: hold Select on a bookmark to delete it, or hold Open on a book in Bookmarks to clear that book's bookmark list.
- Confirmation message after deleting a book's cache from the reader or File Browser.
- File Browser long-press action for deleting an EPUB or XTC book's cache.
- Downloaded-font size range setting so SD-card fonts can use compact, default, or large point-size sets.
- File Browser long-press action for marking EPUB books as finished or unfinished.

### Changed

- Hardened deep sleep entry by shutting WiFi down before waiting for the power button to be released.
- Raised the web file-transfer filename limit from 100 to 150 bytes so longer uploaded filenames are preserved.
- Made the in-reader Reader Options menu include the same Reader settings and actions as Settings > Reader.
- Split SD-card font descriptions and supported languages into separate lines in the font download screen.

### Fixed

- Inline EPUB images no longer disappear in landscape when their bottom edge slightly overlaps the screen margin.
- Reduced unnecessary low-memory image suppression for JPEG-heavy EPUB chapters and added CSS heap diagnostics during chapter rebuilds.
- Allowed wider inline JPEG images in EPUBs to render when they still fit the total pixel and heap safety limits.
- SD-card font picker no longer reopens immediately after selecting a font from Settings > Reader > Font Family.
- In-reader font-size changes now work for SD-card fonts.
- In-reader SD-card font changes now rebuild the current EPUB page layout consistently.

## [v1.2.10] - 2026-05-11

### Added

- `Recent Books View` setting so the dedicated Recent Books screen can switch between the classic list and a 3x3 cover grid.
- More flexible reader controls, including orientation-aware front/side button settings, nav-only or all-button front inversion, tilt page turn shortcuts, and side-button long-press rotation actions.
- Per-session auto page turn interval picker with values from 5 to 120 seconds.
- File Browser Home/Back long-press action for toggling hidden files and folders.
- EPUB rendering and diagnostics improvements, including visible `<hr>` separators and heap logs around section rebuilds, image extraction, page serialization, and sleep-cache rebuilds.
- Reader font coverage for block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation (PR #104).
- Simulator tools for testing sleep/wake behavior and smoke-testing common screens and EPUB reader menus.

### Changed

- Reduced Controls settings section spacing so the grouped controls fit better on X3 screens.
- Made front reader long-press actions trigger when the hold delay is reached while normal page turns still trigger on release.
- Used the fast EPUB spine/TOC indexing path for books with 300+ spine entries so heavily split books build `book.bin` faster on first open.
- Allowed the web file manager and WebDAV to browse dot-prefixed hidden files when hidden files are enabled, matching the device file browser.

### Fixed

- Reader button and shortcut behavior, including X3 power-button wake filtering, folder delete long-press timing, and WiFi scan/connect screens that could not be exited while work was in progress.
- RoundedRaff home-menu, keyboard, and button-hint rendering issues so Settings remains reachable and compact labels no longer overlap or disappear.
- Font and glyph handling now reduces persistent SD-card font advance-cache memory, releases optional font caches before image extraction only when heap is tight, and shows a visible replacement symbol when compact UI fonts lack `U+FFFD`.
- KOReader Sync authentication diagnostics and an in-reader sync crash, including clearer handling when a server or proxy returns non-JSON content.
- EPUB text rendering for redactions, whitespace-only XHTML text nodes, simple black CSS span backgrounds, list bullets in `<li><p>...</p></li>` items, and very long base64-like text runs.
- EPUB image, thumbnail, and section-rebuild stability so image-heavy chapters use less temporary memory, scale images more reliably, avoid stale dimensions, and suppress optional image work earlier under heap pressure.
- EPUB low-memory and cache safety now skips optional next-chapter indexing and sleep-page cache rebuilds when heap is tight, fails safely with a malformed-book warning and Home exit path, rebuilds incompatible fork-written caches, and handles low-memory CSS parsing, truncated SD writes, invalid serialized strings, and failed temp-cache promotion.
- Home no longer crashes after clearing reading cache when the source EPUB cache is missing.
- Reader prewarm behavior now skips image decoding, keeps mixed-style font glyphs cached together, and avoids section rebuilds for render-quality-only option changes.
- Concurrent render/storage crashes are avoided by serializing `GfxRenderer` scratch-buffer access, shared SPI bus access, and failed SPI lock cleanup.
- Recent Books, EPUB/XTC thumbnail caches, deleted-folder metadata, and XTC cover scaling now keep cached book data in sync and grid covers fill their slots correctly.
- Simulator build configuration now lets SDL2 and simulator-provided network/OTA shims compile cleanly.

---

## [v1.2.9.1] - 2026-05-03

### Changed

- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed

- Power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions.
- Potential crash when using `Go to %` in EPUBs.
- Potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid.
