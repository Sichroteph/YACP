# YACP changelog

## 2.0 beta 3 (2026-11-27)

YACP prioritizes battery efficiency, responsive reading and avoiding unnecessary work on the reading path.

### Fixed

- Keep library and book access available after a reading-data import or migration backup fails on a readable SD card, including migrations started by a previous beta. Preserve the old files and use a separate durable store for new preferences and positions.

### Changed

- Clarify that older preferences and positions may not carry over when switching to the CrossPoint base. Issue #16 remains open and its cause is unconfirmed.

## Unreleased: CrossPoint 2.0.0 synchronization (2026-11-15)

YACP prioritizes battery efficiency, responsive reading and avoiding unnecessary work on the reading path.

### Changed

- Use the CrossPoint 2.0.0 EPUB engine for EPUB, TXT and Markdown. Convert saved TXT positions to text anchors and keep the original progress files for recovery.
- Keep saved book positions after rebuilding cached pages, and flush pending EPUB and XTC positions before sleep.

### Added

- Enable opt-in Bluetooth page turners on all Standard builds.
- Bring EPUB highlights and clippings into YACP, with optional clipping synchronization on compatible CrossPoint Sync servers.

### Removed

- Retire the XLarge build variants. Existing installations can move to Standard using the SD card update.

### Fixed

- Save a separate rollback copy when updating an earlier CrossPoint-based YACP installation.
- Import compatible reading and display preferences from CrossInk cards that have no native CrossPoint settings file.
- Honor the selected EPUB Menu hold shortcut on X3 and original X4 when an older Menu binding is also saved.
- Open the dictionary definition directly after holding a word in an EPUB on a touch reader.
- Keep the reading page, clock and progress bar in place during touch dictionary lookup, so a long press selects the word under your finger.
- Restore the Sleep images entry in the desktop and mobile web portal menus.
- Record reading statistics during normal EPUB, TXT and Markdown sessions when tracking is enabled.
- Restore numeric line spacing, Guide Dots, publisher page numbers, reader-only dark mode and forced paragraph indentation.
- Show the correct built-in font and Focus Reading state in the EPUB toolbar.
- Restore grouped settings, the StarDict setup hint and the explanation of Magic Flash refresh and smoothing.
- Restore the OPDS filename format picker and Magic Flash hints in the web portal.
- Preserve EPUB excerpts when moving or renaming a book from the reader or web portal.

## CrossPoint 1.6.5 reconstruction (2026-10-01)

YACP prioritizes battery efficiency, responsive reading and avoiding unnecessary work on the reading path.

### Added

- Open the active dictionary's word history from Home and select a word to read its definition. Swipe through words in a loop or page through definitions, and remove entries with an immediate undo.

- Back up `.crosspoint` automatically on the first boot from CrossPoint or CrossInk. Reinstall your previous firmware and restore the backup to recover your pre-update reading data. See [how to switch back](README.md#return-to-the-previous-firmware).

- Choose Dictionary directly in Controls > Hold Menu (EPUB) on X3 and original X4. Existing Dictionary shortcuts configured through Long-Press Menu continue to work when Hold Menu (EPUB) is set to Default.

- Search Open Library covers from a book's sleep-image gallery, browse other editions, select multiple covers and prepare/upload them in the browser. Fill the screen with cropping by default, with optional whole-image white margins and per-image grayscale adjustments.

- On touch readers, hold a word in an EPUB to open its selected dictionary definition over the book. Swipe vertically inside the popup to browse long definitions, and tap outside to close it without turning the book page.

- On X4 Pro, select Next/Sleep, Sleep/Next or Sleep/Sleep for the side-button short presses while reading EPUB, TXT or XTC books. Sleep saves the reading position and statistics normally; long-press settings remain independent.
- Experimental Magic Flash on X4 Pro with UC8279 LUT 0x68: three factory B/W conditioning passes per text page, with smoothing off and a clean fallback after images or an invalid panel baseline. Select it through the existing Display mode setting; Classic retains its preferences.

### Changed

- Keep Dictionary visible in reader settings when no dictionary is installed, with the expected StarDict folder and file names.

- Show only four alternating rays on the X4 Pro reading status bar's sun icon when the frontlight is off; retain all eight rays when it is on.
- Enable the three-pass, interruptible Magic Flash mode on original X4 units that detect UC8279 LUT 0x68, alongside the already supported Pro. Other X4 controllers, LUT revisions and Classic remain disabled.
- Keep Magic Flash visible but disabled on hardware awaiting support or validation, with an explanation in every language. Classic remains selectable on the reader and web portal.
- Let a queued page turn skip the remaining X4 Pro Magic Flash reinforcement passes after the current pass finishes. Keep three passes when reading stays on the same page.
- Move reading-clock placement to Reader > Clock > Position, independently of status-bar settings, preserving the saved choice.
- Default to Atkinson Hyperlegible Next at 14 pt for new settings, and select Atkinson once when importing CrossPoint settings. Preserve the existing text size and all later YACP font choices.
- Experimental X3 firmware: use three Magic Flash passes per ordinary text page to evaluate contrast and residual screen artifacts on hardware.

### Fixed

- Add space below the dividers in the Home menu and dictionary history, and between the top reading progress bar and centered clock.
- Move the top reading progress bar to the first screen row on X3 while keeping its spacing from the centered clock.

- Keep the first Back press responsive after leaving a reader with remapped buttons. Track a held button and its release by the original physical contact so changing reader profiles or orientation cannot swallow the next press or trigger a second long-press action.

- Hide Open Library suggestions without a cover, failed thumbnails and placeholder images instead of showing empty choices.

- Keep Previous Page, chapter-skip and inherited side-button holds working when the short action is Sleep or disabled. Apply the explicit side-button font-size shortcut to TXT reading, and consume long releases without an extra page turn or sleep.
- Make the existing EPUB Power long-press command list and dispatcher available on X4 Pro, including Join Network. Keep the original labels, action IDs and reader scope.
- Raise the reading status bar's frontlight sun icon by three pixels to align it with the battery and percentage.
- Keep the Quick Resume moon icon visible and correctly rotated in landscape, inside the screen's usable margins.
- Keep settings submenu titles clear of the firmware version, show KOReader server addresses below their label, and shorten the contextual side-button long-press label in every supported language.
- Align the top reading progress bar with the usable screen edge, removing the extra theme margin from the bar, centered clock and reserved text area. Keep the hardware bezel clearance.
- Recognize the X4 Pro model identifier returned by stock CrossPoint during PC-assisted installation, retaining the Pro-only firmware check.
- Keep the top-center reading clock fully visible and below the top progress bar by correcting its font baseline placement.
- Recover reading positions and validated Wi-Fi passwords after an older YACP firmware rewrites an already migrated card. Preserve the complete prior store, resume interrupted repairs and refuse ambiguous positions instead of opening at the beginning.
- Protect PC-assisted installation with verified private reading-data backups, current-source image checks and the migration regression evidence. Keep these installation safeguards on the PC; firmware changes are limited to data migration.
- Restore RoundedRaff button hints, menus and rounded black selections for the YACP theme, preserving its cover-free Home layout and touch shortcuts.
- Consolidate YACP on its CrossPoint base for X3, original X4, X4 Classic and X4 Pro. Restore the top-edge reading clock without extra vertical padding and keep Home menus above the physical button hints, including optional OPDS rows. Preserve matching touch targets on Pro.
- Restore the YACP local network name and the explicit-CA certificate/hostname checks from the archived implementation.
- Guard firmware and simulator compilation with a registered source, unique build ticket, source fingerprint and serialized build wrapper. Retired source trees refuse compilation; personal Go variants stay excluded.

- Returning from a settings subpage restores selection to the parent menu row that opened it.

- Complete the intact black-and-white X3 page when text-grayscale allocation fails, so a Magic Flash base does not remain powered while display power saving is enabled.

- Give settings subpages names that describe their contents, distinguish the screen refresh page from its cleanup method and interval, group automatic backups with reading statistics, and place the clock and battery history under Device.

- Give the cover-free YACP Home card wider white margins on both sides, retaining its reading summary and existing touch or physical navigation.

- Restore the cover-free YACP Home card with the book title, author, progress, recorded reading time and remaining-time estimate. Home no longer reads or generates cover thumbnails. Keep touch shortcuts on X4 Pro and physical-button navigation on X3, original X4 and X4 Classic.

- Restore the optional centered clock above EPUB, TXT and XTC reading pages, with a more legible UI font and a live settings preview. Existing left, right and hidden clock settings retain their values.

- Recheck the hardware clock when Wi-Fi connects and resynchronize it if its date is invalid, even when an earlier synchronization was recorded. Verify the RTC after writing NTP time.

- Record the wake and reset causes in the personal Go build's SD activity log to diagnose missed scheduled checks.

- On X4 Pro, stop the failed saved-network retry before starting Wi-Fi discovery when moving between locations.

- On X4 Pro, retry Wi-Fi discovery after a failed start or an empty scan, including a passive final pass, and log each result for diagnosis.

- Repair saved Wi-Fi networks whose legacy YACP password was wrapped before CrossPoint added its own integrity fields. Preserve already re-entered networks and keep a separate recovery copy.

- Make the frontlight indicator larger and bolder, with a round centre and separated rays that read as a sun, and update it immediately after a reader shortcut changes the light.

- Convert validated YACP `CPV1` Wi-Fi passwords on cards that already contain native CrossPoint settings, while preserving manually re-entered CrossPoint credentials and an exact recovery copy.

- Restore CrossPoint's ordinary Wi-Fi connection path; apply YACP's TCP transport adjustment only during KOReader sync.

- Keep existing CrossPoint settings when a card also contains a leftover CrossInk settings file. Legacy YACP migration now checks the saved YACP identity before moving or converting data.

- On X3 and original X4, the SD firmware recovery shortcut now works after a software restart from a storage error screen. Silent reading restarts still ignore held keys.

- Fix first-boot migration stopping with a general failure on physical SD cards when a migration directory already exists, including retries after an interrupted attempt.

- Firmware now imports legacy YACP settings, progress, book profiles, bookmarks and statistics on the reader's first boot. It retains the original data, checks converted files before activation and resumes interrupted preparation without a PC conversion step.
- Split Controls settings into focused Home Button, Front Buttons, Side Buttons and Touch Reader Controls pages. X3 tilt options remain grouped with side buttons.
- Remove the post-update profile panel. Existing settings are kept, while a first X3 launch selects one-page No Flash maintenance and disables text anti-aliasing.

- TXT reading positions retain the same source passage after font changes and reopening, while accepting earlier CrossPoint and YACP progress files.

- Recover migration text anchors from the legacy cache selected by each book's saved rendering mode, including Balanced and Light, while retaining CrossPoint for new layout.

- Book moves and renames now preserve associated progress, statistics, bookmarks, reader preferences and galleries, reject conflicting destination data and retain a recovery journal after interruption.

### Added

- Contextual two-line help for cleanup-method and frequency choices, above the physical button hints or the touch screen's bottom edge.
- X3 offers two display modes: Magic Flash uses two no-flash passes on every text page with text smoothing disabled; Classic (CrossPoint) restores the user's own cleanup interval and smoothing choices. The locked values remain visible with help on the device and web portal. New X3 configurations default to Magic Flash; existing Classic choices remain selected. Other readers retain their own refresh behavior. The extra pass follows feedback from Reddit user [olimo](https://www.reddit.com/r/XTEINK/comments/1vba8hr/comment/pfe2jdl/). Physical contrast and energy impact still require device testing.
- Restore the Magic Flash and Display Power Saving labels in all supported languages, retaining existing saved settings and defaults.

- Settings on the reader now open short thematic subpages for display, reading, controls and system options, with Back returning to the parent page.

- Reading Statistics and Autonomy are available directly below the book cover on the existing home themes, including the cover-grid variant.

- On readers with a frontlight, the reading status bar shows a battery-sized sun next to the battery. Its center fills when the light is on.

- The side-button long-press action is now configurable on X4 Pro and X4 Classic as well as X3 and original X4.

- The Home key can now put supported readers to sleep on a tap, double tap or long press.

- Restartable offline migration to a verified SD copy, preserving original settings, reading data and unknown records, with explicit review reports and no automatic firmware installation.

- Optional EPUB Menu, Back and Power commands on X3 and original X4, using CrossPoint screens and retaining Pro Home and touch controls. Reader sleep is deferred until input handling returns.

- Per-book publisher page numbers in the reading margin, keeping CrossPoint text layout and rebuilding older generated caches while retaining the saved passage.

- Optional stable reference-page counters from EPUB x-locations manifests, loaded per chapter with bounded parser storage. CrossPoint text layout and ordinary progress remain unchanged; missing or invalid manifests use the normal counter.

- Nearby reading-statistics exchange using the legacy YACP protocol and CrossPoint network navigation, with recoverable peer snapshots and no double counting of temporary or backup copies.

- Canonical YACP logo on default sleep and boot screens and the device portal, retaining CrossPoint attribution and one shared source for future public artwork.

- Web sleep-image preparation with per-image crop and grayscale controls, general or per-book galleries, previews and upload collision protection.

- Per-server OPDS download filename choices, with the CrossPoint global choice retained as the default for new servers.

- Optional X3 periodic screen reinforcement for text pages, retaining full cleanup after images, indexing and explicit refresh. Restore the 60-page interval without changing CrossPoint's saved Never value.

- Keep up to 18 recent books, preserving existing YACP lists when reopening a book.

- Separate recent-book list and nine-cover grid, with the selected book retained from YACP Home, button/touch navigation and lazy cover loading.

- Recent-book actions through CrossPoint list and touch controls: open, reading statistics, reset book-specific preferences and remove from recents. Resetting preferences preserves the previous profile until its replacement has been verified.

- Selective reading-cache cleanup from recent books, the reader and maintenance: preserve progress, statistics and unknown files, stop after a storage error and rebuild only generated data. The confirmation now states that progress and statistics are kept.

- Offline conversion of legacy bookmark records with preserved metadata, explicit ambiguous-position reporting and CrossPoint text anchors.

- Custom device names in CrossPoint settings and KOReader uploads, with retained YACP names and hardware defaults for unnamed readers.

- Verified decoded sleep-image caching for custom and per-book BMPs, retaining CrossPoint grayscale rendering and rebuilding after image changes or interrupted cache writes.

- Optional Guide Dots in CrossPoint text settings. Dots annotate existing word gaps without changing justification, line breaks or saved text positions.

- Physical C3 boot shortcuts: Back for safe Home, Up for SD recovery and Down for Join Network, independent of reading-button remapping.

- A custom 5 to 120 second automatic page-turn interval, stored per book alongside CrossPoint's existing cadence choices.

- EPUB text settings remain specific to their book. Global defaults are restored on exit, with verified alternating saves to retain the previous profile if an SD write is interrupted.

- Optional forward/back tilt gestures, retaining CrossPoint's normal/inverted gestures and the saved direction while disabled.

- Optional C3 front and side orientation profiles, with stable button holds during rotation.

- Independent side-button long presses, including previous page, and font-size shortcuts with retained text position.

- Separate reading-only front-button profiles on X3 and the original X4, using CrossPoint remapping and retaining Pro Home/touch controls.

- Restore the X3 Home date through CrossPoint headers, preserving separate date-sync state and the autonomy date-validity check.

- Optional adaptive grayscale for sleep BMPs, preserving CrossPoint decoding in the existing filter modes.

- Per-book sleep-image galleries retain YACP folder identities and avoid immediate repeats, using CrossPoint bitmap rendering.

- Restore explicit remote SD-firmware installation through the YACP Web API, with CrossPoint image validation and upload-conflict checks.

- Manual global-statistics backups and the X3 automatic sleep backup, with verified writes and interruption recovery.
- Optional top progress bar, with reserved reader space and a matching settings preview.
- Optional X3 current indicator using the existing gauge sample, with no extra display-triggered I2C read.

- File browser choices for one or two lines and hidden extensions, using CrossPoint lists and touch targets.

- Reader-only dark mode, with Quick Resume retaining its polarity while menus keep their own colors.
- Optional paragraph indents, with the original CrossPoint layout preserved when disabled.
- Origin-aware settings conversion experiments with explicit blockers and restartable output.
- Canonical YACP logo sources and project layout for future public documentation.
- Optional numeric line spacing from 70% to 200%, using CrossPoint's slider and existing layout engine. Its four presets remain available.

- Direct CrossPoint 1.6.5 integration branch, preserving its reader, touch controls and dictionary.
- Lexend Deca and Bitter generated with the upstream font converter for Standard and XLarge builds.
- Reproducible source comparison and verified, restartable SD-data snapshots for migration development.
- Default single-book YACP Home using CrossPoint navigation components, with recent-book switching and progress.
- Reading-statistics codecs and EPUB session tracking, including idle rejection and pauses for menus and notes.
- Reading totals, rhythm charts and finished-book history, accessible through CrossPoint menus and touch actions.
- Optional chapter/book time-left estimate in the CrossPoint status bar and cached time left on YACP Home.
- Per-book statistics previews and date editing without double-counting the current reading session.
- Battery-use history and displayed-page counters with the existing YACP data format and a CrossPoint autonomy screen.
- Selected-book statistics from YACP Home, with current-page-inclusive progress.
- One-time completion summary and a final-page confirmation using CrossPoint dialogs.
- Verified backup rotation for per-book statistics and tests using copies of existing SD data.

### Changed

- Replace the built-in Bitter family with Atkinson Hyperlegible Next in all four styles. Standard now embeds 12, 14 and 16 points; XLarge keeps 16, 18 and 20 points. Existing second-family preferences keep their saved slot.

- Compress built-in YACP fonts with Zopfli to reduce firmware size while preserving the same characters, typography and rendered pixels.

- On X4 Pro, the Autonomy page now shows a tappable back arrow in the top-left header.

- YACP Home now gives the book cover most of the upper screen and places quick Reading Statistics and Autonomy actions below it, while retaining the full menu.

- Omit unused italic assets from the UI-only Noto Sans 18 family to recover firmware space.

- Put the tilt sensor in standby outside reading and retry failed SDK power transitions without flooding I2C.

- Select YACP updates by device and font variant, verify release metadata over authenticated HTTPS and check the complete staged SD image before installation.
- Restore the actual previous Wi-Fi power-save mode after HTTP transfers.

- Retain YACP's TCP IPv4 Don't Fragment adaptation during KOReader sync, without changing ordinary Wi-Fi connections.
- Restore YACP Quick Resume defaults through CrossPoint’s sleep and wake implementation.
- Defer SD-font discovery during built-in reading and keep an explicit font preference after a failed load.
- Limit repeated X3 USB battery-gauge reads while keeping button sampling unchanged.
- Retain opt-in Smart Sync with manual choices for equal or uncertain positions and a wrap-safe return timer.
- Group reading-position writes while preserving CrossPoint text offsets and flushing on exit or sleep.

### Fixed

- Consume both screenshot-chord buttons until both are released, preventing a delayed page turn when Power is released first.

- Opening a saved text bookmark beyond a chapter's prepared pages now continues pagination to the requested passage instead of returning to the first page.

- Resolve old SD font-size indices from the captured family's available files, including per-book profiles.
- Retain the X3 display power setting on S3 devices while keeping their screen behavior unchanged.

- Preserve a selected SD font and size when its family is temporarily missing, so it can load again when restored.
- Show CJK book titles and authors on YACP Home when an SD family supplies only a nearby UI size.

### Status

- This reconstruction is incomplete and is not a release or a firmware installation candidate.
- Feature ports, semantic data migration, simulator verification and hardware acceptance remain tracked in `docs/yacp-port-matrix.md`.
- Earlier YACP history and release notes remain in the preserved original checkout.

### Credits

- CrossPoint contributors for the upstream firmware and FreeInk SDK integration.
- YACP, CrossInk and font authors retain their existing attribution and licenses.
