# YACP data migration development

## X4 Pro saved Wi-Fi credential format finding (2026-10-03)

After installing build 340, the X4 Pro still could not connect with its saved network. It connected after the user forgot that network and re-entered the password. The private pre-update backup proves both stored credentials had a valid YACP `CPV1` envelope under one hardware identity. Each encoded 27 bytes: an eight-byte envelope and a 19-byte password. The CrossPoint credential loader does not remove that envelope; it accepted all 27 bytes, then added length and checksum fields to the same obfuscated bytes. Byte-for-byte equality across the update therefore did not establish semantic compatibility, contrary to the earlier provisional diagnosis.

The X4 Pro card had native CrossPoint settings and an unmarked leftover CrossInk settings file. `LegacyMigration::run()` intentionally kept the native settings and returned before `credentials("wifi")`. The X3 source card carries a YACP identity marker and takes the full conversion path. An offline same-device X3 candidate verifies that conversion removes the envelope and yields two 19-byte CrossPoint passwords. The physical X3 card was not available for a post-flash credential comparison at this checkpoint.

The importer now converts only validated `CPV1` records on mixed or native-settings cards before `WifiCredentialStore` loads. It preserves the original file, leaves ordinary CrossPoint and manually re-entered records unchanged, resumes from the preserved input after interruption and does not change the CrossPoint Wi-Fi connection UI or radio path. Pro simulator build 345 verifies a complete conversion and a restart immediately after the backup, alongside the 187-file real-card preservation check.

## X4 Pro card with native CrossPoint and leftover CrossInk settings (2026-10-03)

The Pro SD card supplied for the next hardware test contains both native CrossPoint `settings.json` and leftover CrossInk `crossink-settings.json`. Its 187 files (41,606,391 bytes) are backed up with matching SHA-256 hashes outside the repository at `YACP-archives/2026-10-03-x4pro-card/original`. The native settings use a 16-point font and Home actions 0, 1 and 9; the old CrossInk store uses a 14-point font and a different Home-action enumeration (23, 24 and 25).

Previously, the legacy filename alone triggered the YACP importer. The importer now requires a saved `buttonLayoutPromptVersion` containing the YACP version marker before moving or converting legacy data. For an unmarked CrossInk store with no begun migration transaction, normal CrossPoint loading continues and the old file stays intact. An already begun transaction with an unidentified source is stopped with its original data retained. Existing completed transactions keep their fast path.

The identity check reuses the bounded boot-only JSON reader and releases its document before settings preparation or book scans. It does not run on page turns and adds no persistent buffer. `verify_yacp_mixed_stores.py` tests the real mixed card on a copy, verifies all 187 files at the migration boundary, then boots normally and checks the native font size, Home actions and physical-reader progress files. Native Pro build 325 passes these checks with the simulator storage contract aligned to SdFat. The real X3 source version `1.8.1-yacp.6-tiny` also passes the targeted identity gate, retaining its original settings/state bytes before conversion (`qa-artifacts/yacp-source-gate-325/result.json`). A separate full-card rerun exceeded the harness 100-second limit while the Windows firmware compilation was running; that attempt is retained as incomplete, not counted as a passed full migration. The conversion code itself is unchanged from the previously validated importer.

The existing native theme is retained as a saved preference; select the YACP theme explicitly when checking its Home on Pro. Standard Pro build 324 was interrupted before distribution so that this source-detection correction could be included. Tiny build 326 and Pro Standard build 327 both compile successfully. `YACP-1.7.2-yacp.327-x4pro.bin` is copied to G: after verifying its ESP32-S3/x4pro identity and SHA-256, then verifying the destination copy. The image is 6,528,432 bytes with 25,168 bytes of OTA margin; SHA-256 is `358ed8b42fc47aaca90a70053a8092c6f1a998ef4cb357010cb3c8b54a6d89f0`. All 187 existing card files still match the verified backup. No flash was triggered. Evidence: `qa-artifacts/deposit-pro-standard-327.json`.

After the manual Pro Standard build 327 installation instructions and the requested checks (Congo, touch, Home, frontlight, close/reopen, sleep/wake and restart), Christophe reported that everything appears to work correctly. This records positive initial Pro hardware feedback for that test session. Individual scenarios were not separately itemized by the user, so extended stability and full feature acceptance remain open. YACP Home can be selected under Settings, Display, UI Theme without replacing the other saved preferences.

## X3 hardware migration and persistence feedback (2026-10-03)

After following the Standard build 323 USB installation procedure, Christophe confirmed that the X3 starts. Asked to check Congo's saved passage, the YACP theme and No Flash cleanup on every page, he reported that everything appears OK. This is a positive initial hardware report, not a separately instrumented validation of every migrated record.

Christophe subsequently confirmed that all three requested checks passed: progress after advancing, closing and reopening Congo; sleep and wake; progression and preferences retained after a restart. These scenarios are now validated by user report on X3 Standard build 323. The recovery shortcut itself, extended reading/statistics and the other hardware targets remain pending. The private source-card and full-flash backups are retained.

## Recovery shortcut limitation in builds 314 and 322 (2026-10-03)

The previous instruction to use the side-button plus Power shortcut was insufficient from the general-failure screen. That screen restarts through `ESP.restart()` after a two-second Power hold; the old C3 boot routing only sampled shortcuts for a `PowerButton` wake. A software restart is classified otherwise, so repeated attempts never opened recovery. Build 323 samples stable physical buttons on non-silent C3 boots and permits only firmware recovery outside a power-button wake. Other shortcuts retain their wake policy; silent reader restarts ignore held keys. Eleven host regression cases cover software reset, normal wake, unstable input and silent restart.

An already installed build 314 cannot gain the fix from an SD file it cannot install. USB recovery was therefore prepared for the identified X3, with no automatic flash. The read-only USB inspection identifies ESP32-C3 and the user's recorded MAC, current build `1.7.2-yacp.314-tiny`, active application at `0x10000`, and the legacy partition layout (app1 at `0x780000`). Do not use the newer repository partition table to overwrite this reader's boot metadata. The accelerated read stub repeatedly stopped near `0x376000`; direct ROM reading succeeds there. The manual recovery tool consequently uses ROM mode and retains the existing bootloader, partition table, OTA selection and SD data.

`scripts/recover_yacp_usb.py` inspects a prepared private plan without touching the device by default. Installation requires `--install`, a verified full flash backup, the expected image identity/hash, the uniquely matching USB serial, unchanged live boot metadata, and the user's typed `INSTALLER`. It writes only the active application range and lets esptool verify the transfer. This is a one-device recovery procedure, not a public installer.

The complete 16 MiB flash backup is now saved outside the repository at `YACP-archives/2026-10-03-x3-usb-recovery`. Boot metadata and the problematic read region match independent reads, and the complete active application matches the archived build 314 byte-for-byte. Standard build 323 passes Tiny compilation with an image of 6,474,192 bytes. Its manual launcher and plan are under the archive installation directory. Offline plan validation succeeds; no USB write or firmware installation was performed by the agent. The subsequent X3 migration and persistence feedback is recorded above; extended reading checks and recovery-shortcut validation remain pending.

## Physical SD failure in build 314 (2026-10-03)

The X3 reported `Error: General failure`. A full SHA-256 verified snapshot of the returned card is retained outside the repository under `YACP-archives/2026-10-03-migration-314-failure`: 1,397 files, 1,156,029,451 bytes. The migration root exists but is empty, and the original `.crosspoint` has not been moved. All 1,125 retained source files other than runtime state match the pre-trial snapshot; `state.json` changed and the transient `sleep_frame.bin` is absent. Legacy settings, book progress and statistics remain present.

Cause: `Migration::run()` creates the transaction root, then `Io::append()` calls `Io::parent()` which calls `mkdir` on that same root. SdFat uses exclusive creation for the final directory component and returns false if it already exists. The native simulator previously returned true for `EEXIST`, hiding this fault. A second physical boot fails at the already existing root itself. Neither attempt reaches the first transaction marker or original-directory rename.

The importer now uses the existing HAL `ensureDirectoryExists` contract for every migration directory. The SDK verifies an existing directory and closes its handle, or creates missing parents. Existing regular files remain errors. This changes no data format and needs no new application buffer.

For native acceptance, apply `scripts/patch_yacp_simulator_storage.py` to the local simulator dependency's `src/HalStorage.cpp` before compilation. It aligns both `mkdir` and `ensureDirectoryExists` with the verified SDK contract. The migration suite additionally covers the exact empty-root state and a regular-file collision, alongside the interrupted preparation cases. Tests use a copy of the returned card, without resetting its pending migration directory.

Native build 321 passes the strict-directory acceptance suite: four interruption boundaries, an empty pre-existing transaction root, a conflicting regular file, bookmark formats 2 to 5 and a 1,024-record list. On the returned raw-card copy, 78 settings, 11 positions (Congo: spine 22, visible offset 990) and five profiles match independent codecs, and archived original files retain their hashes. Evidence: `qa-artifacts/runtime-sdfat-faults-321/fault-result.json` and `qa-artifacts/runtime-sdfat-real-321/result.json`.

Standard firmware `YACP-1.7.2-yacp.322-tiny.bin` passes `pio run -e tiny`: 6,474,192 image bytes, 79,408 bytes of OTA margin, 58,856 static RAM bytes. Its embedded version/variant and image digest were verified before copying to G:, then the copy hash was verified. SHA-256: `cb040f11b0d7585d2c0807374d47bd46c8aeb829dcd596d60e3f4195a9461cc3`. Only this firmware file was added to the card. No flash was triggered.

Hardware readiness remains false until the corrected image completes migration on the X3. Installation must use the firmware recovery picker if the error screen prevents normal settings access. On the C3 path in build 314, the picker can only be entered on a genuine power-on or deep-sleep wake. It is unreachable through the error screen software restart; see the USB recovery correction above. Installation remains manual.

## Current correction: migration runs inside the firmware (2026-10-02)

The X3 trial of build 297 exposed a release-blocking omission: only the PC copy converter was available. A raw legacy SD card therefore loaded CrossPoint defaults and used a different progress cache. The earlier prepared-copy experiments below did not validate that first-boot workflow.

`LegacyMigration::run()` now runs immediately after SD initialization, before settings, state, statistics or credentials can be loaded or resaved. No PC preparation is required. The reader retains the entire old `/.crosspoint` directory as `/.yacp-migration-v1/original`, prepares converted data separately and activates it only after checked writes. Books, fonts and sleep images outside that directory stay in place. The original directory is never removed by migration.

The importer handles the legacy settings file, old font indices (Standard or XLarge from the saved version and actual SD font catalogue), FNV64 and ESP32 cache identities, per-book text profiles, unique EPUB text anchors, TXT/XTC progress, statistics, history, recents, state and credentials. Bookmark versions 2 to 5 are converted to CrossPoint anchors using unique saved text or the matching old chapter cache. Large bookmark lists stream one record at a time. Unmatched or ambiguous records are retained in the original directory and listed under `/.crosspoint/migration-review`; they are not silently assigned a guessed book or passage.

Wi-Fi, OPDS and KOReader credentials are imported only when their legacy integrity envelope matches the running reader's factory identity. Moving a card to another device cannot recover a password bound to an unavailable source identity. A failed credential conversion preserves the original file and emits a review record without exposing passwords in logs.

The native first-boot experiment uses a private copy of the raw post-trial SD, not a PC-converted card. Evidence in `qa-artifacts/runtime-real-308b/result.json` verifies 78 target settings, 11 identifiable reading positions and five book profiles against independent Python codecs. Congo retains spine 22 and visible-text anchor 990. Opening it in the X3 simulator reaches the page containing that anchor; leaving the reader saves that page's own start offset. Original files match their pre-migration hashes. The four historical caches with unidentified current book paths remain archived and explicitly reported. The simulated device also reports the expected factory-key mismatch for the real Wi-Fi file. Separate synthetic tests pass all three credential conversions with the simulator's own key.

`verify_yacp_runtime_migration.py` executes the compiled firmware and interrupts it after original preservation, settings preparation, full preparation and activation. It also truncates a staged settings file, a bookmark fragment and the completion marker. Restart completes migration and preserves source hashes. A second normal boot does not repeat conversion. The tests cover bookmark versions 2, 3, 4 and 5, including 1,024 records. These are software tests, not proof of physical FAT/SD behavior during power loss.

The first boot after an update keeps the existing settings without presenting a profile chooser. A first X3 launch writes No Flash maintenance for every page and disables text anti-aliasing; existing saved choices always win. The YACP Home again shows an upright grey book and its centered white logo.

The corrected Standard X3 image is `YACP-1.7.2-yacp.314-tiny.bin`, copied to G: with its embedded identity and SHA-256 verified. Only that new firmware file was added; the existing 1,300 source files were checked without differences. Earlier build 309 was withdrawn from G: and preserved in the private archive after the final visual review found a remaining upstream Home icon. Build 314 uses the canonical generated YACP logo, visually checked in native build 313.

Hardware acceptance remains required: install the verified Standard test image manually on X3, wait for first-boot migration, keep current settings, open Congo, verify the passage, leave and reopen it, then test sleep and wake. Do not clear caches or copy a PC-generated settings file before this test. Inspect the firmware version to distinguish it from build 297. No automatic flash is authorized.


## Current complete-copy checkpoint (2026-10-02)

This section supersedes the historical conversion checkpoints below. The latest source is the returned Standard X3 card, read from `G:` without writing to it. Its complete reader-file snapshot is `C:/DEV/Projets/C-Pebble-Ubuntu/XTEInk/YACP-archives/2026-10-02-crosspoint-migration/sd-standard`: 1,279 files, 1,138,879,384 bytes, with SHA-256 verification. Only the operating-system directories `.fseventsd`, `.Spotlight-V100`, `.Trashes` and `System Volume Information` were excluded. The older source archives remain intact.

`scripts/migrate_yacp_copy.py` now assembles a complete, restartable **offline copy**. It does not write a mounted reader, replace the current firmware or import automatically on boot. A full SD snapshot is required, including books and fonts, rather than only `.crosspoint`. To prepare a snapshot from an already copied and readable local SD tree:

```powershell
python scripts/prepare_yacp_migration.py C:/local-sd-copy C:/local-archives/yacp-complete --source-variant tiny
python scripts/migrate_yacp_copy.py C:/local-archives/yacp-complete C:/local-archives/yacp-converted
```

Use a new output directory on a fixed local disk, outside the snapshot and Nextcloud. When preserving device-bound credentials, supply the factory identity explicitly with `--source-mac <factory-mac> --same-device`, or use `--target-mac <other-reader-mac>` for another reader. Without both identities, encrypted stores remain archived and the report requests re-entry. Do not commit the prepared card or credentials.

The default cache policy rejects ambiguity. `--source-cache-policy yacp-fnv64` is appropriate only after confirming that the source firmware used FNV64 cache identities. This was verified in the frozen `reference-source/lib/Epub/Epub.cpp:233` for this card. It selects the active FNV64 Alice cache and archives its older GCC32 cache, without comparing percentages or timestamps. The earlier twelve validated EPUB records cover eleven unique books, including these two historical Alice positions.

### Publication, retry and original preservation

The output contains `intent.json`, staging in `card.pending`, the finished `card` directory and `COMPLETE.json`. Every destination path, size and SHA-256 is pinned before copying. A directory rename publishes `card` only after all payloads and the immutable source snapshot have been verified. Repeating the identical command resumes matching partial files or checks the completed result. Divergent files, source changes, linked paths and unrelated destination contents are rejected without overwrite. Process interruption is tested after the intent, every file, verification, publication and completion marker. This is not a claim of physical SD power-loss validation.

All original `.crosspoint` files remain byte-for-byte under `card/.yacp-migration/original-crosspoint/`; books, fonts, images and other files retain their original paths. The independent full source snapshot is the rollback reference. Existing archives must remain available even after a future device trial.

`card/.yacp-migration/report.json` lists conversions and unresolved records. Four unidentified saved positions are preserved without guessing a book or percentage. Other unmatched caches and excluded or unknown stores are also archived. They do not prevent preparing the supported data. An unresolved currently open book disables automatic resume of that book, rather than inventing a position. Temporary sleep and overlay anti-repeat directory indices are reset; image files and converted preferences are retained. Reconstructing the old directory enumeration is no longer required.

### Verified complete copies

The private `esp32-v2/card` contains 1,364 files with eleven active EPUB positions, five matched book profiles, twelve recents, settings, state/autonomy, statistics and two converted Wi-Fi networks. All seven original book profiles are preserved: five are attached to identified books and two remain with unmatched archived caches. Its eleven review records identify archived exceptions; none was discarded. A repeated migration produced the same verified intent and payloads. Wi-Fi authentication still requires the actual reader.

The independent `native-v2` copy uses native simulator cache hashes and deliberately omits active real credentials. X3 build 295 and Pro build 296 both open the real current Congo passage and return through the reader controls. Verification checks 79 converted settings with explicit test overrides, twelve recents, 25 protected stores, unchanged book bytes and the complete prepared-card manifest. Daily history upgrades from v1 to v2 while retaining every minute and its anchor day; its prior primary becomes the normal backup. Only the current book's derived remaining-time estimate is recomputed. Historical durations, page/session counts, dates and flags are unchanged. The original backup also remains in the archive.

The aggregate report is `qa-artifacts/complete-migration-acceptance-2026-10-02.json`. Evidence is in `qa-artifacts/complete-migration-x3-295/result.json`, `qa-artifacts/complete-migration-pro-296/result.json` and `qa-artifacts/complete-migration-state-host/`. The production state codec round-trips twelve supplied fields under address/undefined sanitizers. All 118 Python tests pass, including thirteen complete-copy cases for interruption/retry, source mutation, destination collisions, corrupt books, unknown formats and TXT/XTC anchors. Real data contains no legacy binary bookmarks; bookmark conversion is covered by synthetic fixtures and the earlier X3/Pro navigation scenarios.

TXT progress now stores a source byte offset in the legacy YACP six-byte record and accepts CrossPoint's earlier four-byte record. X3 295 verifies exit/reopen, font-size repagination and old-record upgrade, with the existing write debounce retained. This changes persistence, not CrossPoint text layout.

### Remaining acceptance

The copy is prepared and software-verified, not installed. Test on an expendable SD copy and X3 first, then Pro, original X4 and Classic. Verify resume/progress, bookmarks, statistics, fonts, images, SD, sleep/wake and the same Congo passage. On Pro also verify touch, Home and frontlight. Test real Wi-Fi and synchronization on the device, and check C3 heap and physical refresh behavior. Physical interruption tests must use disposable copies. Final Home appearance remains subject to visual acceptance. No flash, commit or publication has occurred.

## Historical conversion checkpoints

## Source and isolation

The local firmware tree, including uncommitted changes, is the behavior reference.
The published comparison point is [YACP 1.7.1](https://github.com/Sichroteph/YACP/releases/tag/v1.7.1-yacp), commit `50068816c43e459bf04627370069c20143e20b49`.
The local checkout HEAD predates this published tag. A difference from local HEAD does not by itself identify an unpublished feature.

The supplied SD card is `G:` and was last configured with **Standard**.
Read-only source captures are stored outside the repositories and Nextcloud:

`C:/DEV/Projets/C-Pebble-Ubuntu/XTEInk/YACP-archives/2026-10-01-crosspoint165/sd-standard/`

Every captured file has a SHA-256 manifest. Settings and network credentials remain in this private local archive, not in the repository or public documentation.

## Preparation tools

- `scripts/prepare_yacp_migration.py`: copies a selected source tree, checks every file, resumes a matching interrupted copy, rejects a different source snapshot or variant, and never overwrites divergent destination data.
- `scripts/yacp_data_formats.py`: strict readers for font indices, progress records, bookmark versions 2 to 5, daily-history versions 1 and 2, book-statistics versions 1 to 6, global-statistics versions 1 to 3, and finished-book index versions 1 to 3.
- `scripts/audit_yacp_data.py`: produces an audit from the copied `.crosspoint` directory. It does not install or activate migrated settings.

Example, using a previously copied SD data directory:

```powershell
python scripts/prepare_yacp_migration.py C:/local-sd-copy/.crosspoint C:/local-archives/yacp-sd --source-variant tiny
python scripts/audit_yacp_data.py C:/local-archives/yacp-sd/original --source-variant tiny --output C:/local-archives/yacp-sd/audit.json
```

`copy_verified: true` means that the source bytes were preserved. `migration_validated: false` remains explicit until firmware-level migration acceptance passes.

The supplied snapshot passes the format audit for 19 EPUB cache directories. The audit decodes statistics in each versioned file independently rather than adding overlapping versions together. Finished-book path keys are represented as decimal strings to preserve all 64 bits in JSON consumers. Original title bytes remain available when an old fixed-length field ends inside a UTF-8 character.

Run the host checks with `python -m unittest discover -s test -p 'test_yacp_*.py'`. The current 26 checks cover restartable snapshots, refusal to overwrite different data, font indices, truncated formats, statistics flags and dates, finished-book identifiers, lossless daily-history conversion, and text-position candidates. They do not replace a firmware migration test.

The C++ statistics harness in `test/yacp_statistics/` compiles the actual firmware codecs against an in-memory filesystem. Sixteen scenarios exercise midnight, missing RTC, interrupted writes, backup recovery, future formats, synchronized totals, session thresholds, pauses, pace warmup, clock wrap, completion identity, and previews with date edits, and bounded time-left estimates. `scripts/verify_yacp_stats.py` also compares C++ round trips against the independent Python decoders for 18 real per-book files, two global files and two daily-history files. All those round trips preserve the decoded data.

`scripts/yacp_legacy_anchor.py` recovers reviewable visible-text offsets from version 27 and version 44 section caches and source EPUBs. Version 27's header and separate word strings are verified against the local CrossPoint `1.4.1` tag. It rejects short, ambiguous and missing anchors. Unresolved OPF item references are skipped exactly as in both firmware parsers. On the supplied snapshot, eleven of sixteen progress records currently have candidates. Five remain unresolved: four cache IDs lack a supplied matching book path, and one saved page has no section cache. These records remain preserved. Candidate generation alone does not validate firmware navigation.

The offline cache mapper recognizes both current YACP FNV-1a 64-bit identifiers and GCC's 32-bit string hash. The latter matches two pre-existing card cache names exactly. Its algorithm and seed are checked against the installed GCC 14.2 toolchain and [GCC hash_bytes.cc](https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/libstdc%2B%2B-v3/libsupc%2B%2B/hash_bytes.cc). Multiple matching paths are rejected instead of choosing one. Native simulator cache IDs remain separate from ESP32 IDs.

## Current font-conversion correction

Candidate v14 supersedes the font-size values in candidates v1 through v13. Reading the frozen registry revealed that `selectFile()` chooses the indexed **available family size** before considering the nominal SD size range. The previous range-only conversion was incorrect when the installed catalogue differed from that range. No candidate was installed.

Conversion now requires the SHA-verified font snapshot (`--fonts-snapshot`), maps against its actual sorted sizes, and blocks SD size conversion if the catalogue is missing. For this captured card, the global Atkinson preference resolves to 12 pt and the Congo book profile to 16 pt. The independent Standard/XLarge mapping still applies to built-in fonts. Early 16/20-point rendering experiments remain layout experiments and do not establish complete settings equivalence.

## Confirmed format differences

1. YACP uses `crossink-settings.json`. CrossPoint uses `settings.json`; both files exist on the supplied card. The latter must not silently take priority.
2. YACP's built-in font index depends on the firmware variant. For SD fonts, the frozen registry prioritizes the index into the installed family's sorted sizes; `sdFontSizeRange` supplies only a fallback target. The supplied Standard card's global index 0 selects **12 pt** in its captured Atkinson catalogue. The Congo book override selects **16 pt**. See the correction above; a range-only conversion is invalid for this card.
3. YACP themes and sleep modes have different stored IDs. For example, YACP theme 6 identifies its Home, and sleep mode 9 identifies Quick Resume. Raw enum copying is unsafe.
4. Current YACP EPUB cache paths use FNV-1a 64-bit book-path hashes. CrossPoint 1.6.5 uses `std::hash`. The supplied card has both long and shorter cache IDs. Every book's progress and statistics must be mapped explicitly before regenerating layout caches.
5. Supplied progress files are 6-byte spine/page/chapter-page records. They do not carry the newer visible-text-offset anchor. Preserving these bytes does not prove identical displayed content after repagination.
6. YACP bookmarks are binary records containing paragraph anchors, timestamps and snippets. CrossPoint bookmarks are JSON with XPath, percentage and optional visible-text offsets. The supplied snapshot has no bookmark files, so synthetic fixtures cover decoding but do not establish real-book navigation correctness.
7. Daily history starts with `CRHM`, followed by its version byte and anchor day. Version 1 has 730 byte-sized values; version 2 has 730 little-endian 16-bit values. Conversion of the supplied version 1 file preserves every bucket and is idempotent. Previously saturated days cannot be reconstructed from that file alone.
8. YACP stores autonomy and additional sleep/gallery fields in the shared `state.json`. Loading and rewriting only CrossPoint's known fields would discard those fields. No first-boot importer is enabled yet.

## Acceptance still required

- Explicit field mapping and retained settings for every selected YACP behavior.
- Preserved open book, recents, finished-book index, bookmarks and autonomy data.
- Exact or explicitly reviewed content positioning after cache rebuild, including the reference EPUB passage.
- Interrupted migration, retry, and rollback using the preserved original data.
- Firmware readers verified against the copied statistics, including time zones and daily history.
- No device installation until the feature matrix and migration checks are complete.

## Native reader experiments

`scripts/prepare_yacp_reader_fixture.py` creates a new isolated test SD containing one copied book, its captured font family and a ten-byte progress candidate. It copies no network credentials. `test/yacp_statistics/CacheIdentity.cpp` computes the native compiler's `std::hash` identifier; this is intentionally not an ESP32 cache identifier. The fixture uses English UI and preserves selected reading settings whose value meanings have been compared. It is not the complete settings migration.

`scripts/verify_yacp_reader_fixture.py` checks the actual saved progress against CrossPoint's version 48 visible-text table. Committed partial sections are accepted only with a valid watermark. An unresolved target beyond the verified portion of a partial chapter cannot pass as a resumed position. A visual check must also confirm the expected text on the page.

Three private fixtures passed. The newly decoded version 27 cache for the renard book resumes at spine 4, page 3, with the recovered text offset 1073 on the displayed page starting at 903. Its committed partial chapter contains eight verified pages, and the recovered line is visibly present. Earlier fixtures: Congo, chapter 22, offset 0 on Pro; and the Ackroyd book, chapter 19, requested offset 1059 contained in the X3 page starting at offset 1047. Original files remain in the external archive. These results do not activate a migration or authorize installation.

## Autonomy compatibility check

The new firmware state codec reads and writes the supplied `powerHistory` object without changes in a host round-trip test. Its bounded samples reject invalid percentages, out-of-range minute values and decreasing time entries. The Pro simulator also displays this object from an isolated fixture containing no network credentials. This validates the autonomy object only: the remaining gallery, pending navigation and settings fields still need the complete first-boot migration.


## Explicit settings experiment

`scripts/prepare_yacp_settings_candidate.py` accepts only a SHA-verified snapshot with its recorded source variant. It reads the YACP-specific JSON, never silently substitutes a coexisting CrossPoint settings file, and writes separate candidate/report files. It resumes matching partial output and refuses conflicts, changed source files, overlapping paths, linked destinations, the reference SD drive and Nextcloud. No source values or credentials are printed.

`scripts/yacp_settings_migration.py` records a decision for every supplied key. Unported or unknown values are blockers, including adaptive sleep filtering and several controls. The supplied card has 76 keys, of which 24 remain pending in candidate v4. Defaults for absent keys have not yet received full origin-aware migration; the candidate always reports `ready_to_activate: false`. In candidate v1, forty-nine generated values round-tripped through a private Pro simulator reader fixture, with English and disabled statistics as test overrides. Complete state, recents, progress, backups and activation still require their own migration.

The reader experiment verifier now accepts both upstream section 48 and YACP section 49, including their distinct partial markers and validated visible-text tables. These caches are reconstructible; they are not copied over the original YACP sections.

## Device-bound Wi-Fi credentials

The frozen YACP credential writer uses a CPV1 envelope with a device-keyed FNV checksum. CrossPoint 1.6.5 writes raw XOR/base64 credentials with separate length and CRC32 fields. Copying a current YACP Wi-Fi file directly into the new store is therefore not a validated migration, even on the same device. Moving it to another reader also requires the source and target device keys or renewed credential entry.

`scripts/yacp_credential_migration.py` implements a pure, non-installing conversion with explicit keys and source format. It rejects wrong keys, corrupt envelopes, excess networks, duplicates and unknown fields as a whole. Pre-CPV1 input requires an explicit reviewed-legacy choice and remains marked unverified. Seven synthetic tests cover these boundaries, empty/max-length passwords, rekeying and the target length/CRC. All 52 Python checks pass. The real snapshot contains two networks without separate integrity fields; no real password was decoded, printed, converted or activated. The full first-boot migration still needs to supply and validate the appropriate hardware identity.

## Current conversion checkpoint

Candidate v11 accounts for all 76 supplied settings keys, with 12 unsupported values still blocking activation. Its 66 generated values round-trip through Pro 191 on a private copied reader fixture. This supersedes the coverage count above, without changing the earlier experiments or their evidence. The migration still does not activate automatically, and a completed settings conversion alone cannot authorize replacing state or progress files.

Four orphan cache identities have no corresponding EPUB among the supplied copies. Their metadata identifies Harcelement by Michael Crichton, two Chinese books and one book with empty title/author. The metadata includes two legacy 32-bit cache directories with an unprefixed version-8 header and two FNV64 caches with the later magic-prefixed header. Identity hints are retained privately in `sd-standard/unresolved-book-identities.json`; they do not prove a new path or justify attaching progress to a different book. User clarification about keeping these as archived experiments versus migrating active readings is pending. The fifth unresolved position belongs to L'Homme Terminal, where the book exists but the saved page has no corresponding section cache.

The source card was no longer mounted as G: during this checkpoint. No write to the card occurred. The complete original YACP snapshot and original checkout were rehashed: 5,747 files checked in each, with zero mismatches (`qa-artifacts/source-preservation-orientation.json`). All six registered worktrees remain present.

## Tilt conversion checkpoint

Candidate v12 accounts for the same 76 source keys with nine remaining blockers. It maps the old tilt direction schema explicitly, including the historic left/right sign change, and retains the selected gesture while tilt is off. Future or invalid schemas are rejected as a group. The real captured Standard preference maps to off with the CrossPoint normal direction remembered. All 58 Python checks pass. The actual HAL passed 16 axis/orientation cases and power-transition failure tests under address/undefined sanitizers; physical gesture and current-draw tests remain required.

## Per-book profiles and reference passage

The copied card contains seven `reader_settings.bin` v2 records (86 bytes), six with active text overrides. They are preserved user data, even though they live inside old EPUB cache directories. The seventh selects an old experimental rendering mode without overriding text. Candidate v13 adds these records using `prepare_yacp_settings_candidate.py --include-books`, verifies the source snapshot and produces identical output on a repeat run. Sixty-one Python checks pass. At that checkpoint runtime book-specific settings and transactional activation were not implemented; later runtime checks are recorded below.

The frozen reader computes a nominal target using the global SD range, but its registry gives priority to the indexed available family size. Candidate v14 follows that effective selection. The book record stores neither the catalogue nor the range. Unknown flags, future versions, wrong lengths and unsupported field values remain explicit failures or blockers. Old CrossInk/Balanced/Light engine choices are archived because the approved base is the CrossPoint layout engine.

This discovery narrows the earlier photo-passage claim: those experiments used the global settings. The first per-book experiment incorrectly converted the profile to a 20 pt target with 90% line height. Pro 191 snapped it to the largest installed file, 18 pt, and resumed spine 22 at visible-text offset zero. This exposed the catalogue-index mistake corrected in v14; that experiment is not proof of faithful font migration. The screenshot was reviewed and does not reproduce the photo's extreme spacing. The verifier confirms the anchor inside the committed portion of the new cache (nine pages available, chapter still partial), not complete chapter pagination. This is a single-book experiment, not proof of runtime profile isolation or identical photographed settings.

The corrected v14 book profile was then checked on X3 simulator 197: all generated settings round-trip unchanged, the reader resumes spine 22 at offset zero, and the 16 pt SD font ID (798274874) exactly matches the preserved YACP v44 section header. The screenshot was visually reviewed. This establishes the effective font for that saved passage independently of the conversion formula. The later runtime profile checkpoint below verifies isolation.

## Runtime book profile checkpoint

The corrected v14 global fixture round-trips all 68 values on Pro 201, including the C3 fading preference which stays inactive on S3. Its rendered page is pixel-identical to Pro 191 with the same corrected font settings. Repeating v14 candidate generation preserves the source manifest and produces identical output.

X3 203 loads the converted real profile through the new book-scoped storage, not through global settings. The profile uses Atkinson 16 pt and the global settings keep Atkinson 12 pt. All 68 global values remain unchanged on exit; the profile file is unchanged as well. The saved text anchor remains spine 22, offset zero, and the chapter finishes pagination with ten pages. The screenshot was reviewed. This is still a fixture with explicitly prepared data, not automatic first-boot migration.

The new profile codec is `scripts/yacp_book_profile.py`. Runtime files are under `/.crosspoint/book_profiles/epub_<native-hash>/reader_profile.{0,1}` and survive ordinary cache clearing. The original YACP binary remains in the verified snapshot. Profile activation for all migrated books and unsupported legacy preferences remain pending. The interval implementation is recorded below.

## Profile version 2

Candidate v15 adds explicit target profile flags and the optional automatic-turn seconds. Repeating the conversion produces matching outputs and preserves the source snapshot. The runtime accepts the earlier text-only target profile and stores new changes as v2. Text and interval overrides are independent; this distinction also preserves an old interval-only profile without capturing unrelated global text settings. All seven real source profiles lack a saved custom interval. Transactional activation, unresolved legacy preferences and remaining data classes still prevent treating the complete migration as ready.

## Profile version 3 and current candidate

Candidate v16 includes Guide Dots and the version 3 target profile. Its seven book records and all 76 supplied global keys produce identical output on repeated conversion, with eight global blockers. Original snapshot hashes remain unchanged. The 65-byte runtime codec reads earlier versions 1 and 2, inherits the global guide default where it was absent, and separates interval-only overrides from text overrides. Host fault tests cover truncation at every byte, corruptions, unknown future versions, and interrupted writes. All 64 Python checks pass.

The independently built pristine CrossPoint 1.6.5 simulator and YACP Pro 212 render the photographed Congo passage with identical pixels and text geometry when given the same copied EPUB and Atkinson 16 pt settings. This validates retention of upstream layout, not reconstruction of the unknown settings used for the old CrossInk photograph. Migration activation, missing legacy positions and physical-reader tests remain outstanding.

## Omitted fields and clipping audit

Candidate v17 explicitly audits omitted global fields against the 85-field frozen source inventory. The supplied card omits eleven members: aliases, superseded legacy presets, runtime-only fields, empty legacy OPDS strings, the statistics switch and progress-bar position. The two effective defaults are emitted explicitly as tracking on and bottom progress bar. Other unreviewed omissions in older/sparser files now block the experiment instead of silently using CrossPoint defaults. Per-book conversion keeps its separate inheritance rules. The real card still has eight explicit-key blockers and no unreviewed omitted fields; no migration is activated.

The data auditor also decodes frozen ClippingStore version 1, preserving its page/word/paragraph hints, timestamp, text and exact raw bytes when a bounded old field split a UTF-8 character. Tests reject every truncated record, extra trailing bytes, unknown versions and oversized counts/text. This audit does not yet provide annotation UI or establish CrossPoint navigation anchors. All 71 Python checks pass.

The updated audit finds no bookmark or clipping binary in the copied `.crosspoint` tree. This does not establish whether separate text exports existed elsewhere on the original card.

Device-name conversion now preserves the current name and validates the old UTF-8-byte limit without truncation. The target's empty label selects a model-specific YACP default. Its runtime API and settings UI are verified in Pro 226 and X3 227. The 72 Python checks pass; no candidate has been installed.

## Shared state and recents checkpoint

Candidate v20 adds `--include-recents --include-state`. The converter reads the target loader's capacity from its actual header and refuses to truncate the twelve saved recent entries to the upstream limit of ten. YACP's original capacity is eighteen. Every entry, order and metadata field remains in the separate proposal. Unknown fields, unsafe paths and duplicates are explicit blockers. File availability is not guessed from a partial book-copy fixture.

The proposed shared state preserves the open path, all autonomy counters/samples and the sixteen-entry sleep-image ring. Empty favorite/folder preferences and neutral pending-navigation sentinels are archived with an explicit decision; nonempty preferences or active old navigation block conversion until handled. The old Quick Resume framebuffer flag is archived, and activation requires a cold boot. The separate upstream overlay-image ring starts empty. No old screen cache is activated.

All twelve proposed state fields survive a round trip through the actual `CrossPointState.cpp` under address/undefined sanitizers (`qa-artifacts/state-candidate-v20-host`). The gallery ring stores directory-entry indices, so the corresponding file enumeration must still be verified before activation; a sorted copy manifest alone does not prove that order. Candidate v20 has seven settings blockers, one recent-capacity blocker, no unknown shared-state fields, and remains disabled. Eighty Python checks pass. This is not a first-boot installer.

## Candidate v21

The loader now retains eighteen recent entries. Candidate v21 therefore preserves all twelve real entries without a capacity blocker. The X3 periodic refresh preference and 60-page interval have explicit semantic mappings; old Never remains distinct. Repeating generation produces identical output and verifies the source hashes again. The proposal covers 76 supplied keys, seven book profiles, twelve recents and shared state, with six remaining settings blockers and no unreviewed omitted defaults. Gallery enumeration order and full transactional activation remain unverified. No candidate is installed.

## OPDS conversion experiment

Frozen YACP and CrossPoint both use `/.crosspoint/opds.json`, but their credential envelopes differ. The pure `yacp_opds_migration.py` converter requires explicit source and target hardware keys. It verifies the old CPV1 envelope before re-encoding the password for the target and fails as a unit on corruption, unknown fields, conflicting credential representations or more than eight servers. It performs no network requests, credential discovery or writes. The reviewed experiment bounds each field at 4,096 UTF-8 bytes; larger values remain an explicit blocker, not a truncated result.

An existing source store is authoritative, including an empty one. Legacy single-server fields are used only when the store is absent, matching the old loader. Original fields remain archived. Frozen YACP stores a download filename choice per server, while CrossPoint stores one global choice. Uniform choices can map exactly; mixed choices remain blocked until their behavior is ported. Synthetic tests cover rekeying, longer OPDS passwords, legacy migration, empty-store precedence, capacity, corruption and mixed naming choices. All 85 Python tests pass. Real device identity, target runtime codec verification, authentication and activation remain pending; the captured card has no OPDS store.

X3 231 also passes selection of periodic reinforcement and 60 pages through the actual settings screen. The initial UI script incorrectly expected a popup for a two-choice value; CrossPoint toggles that value directly. The corrected navigation preserves this upstream behavior, and English screenshots were reviewed.

The subsequent OPDS runtime block resolves the mixed-filename limitation described above. The converter now writes each source choice to its corresponding server record and leaves the global CrossPoint choice unchanged. The record codec, UI Save/Cancel and HTTP reload checks pass. This does not resolve source hardware identity for actual encrypted/obfuscated network credentials or authorize activation.

## Candidate v22

The recent-book preference now maps explicitly: 0 selects the list, 1 the cover grid. Invalid, boolean, string and out-of-range values remain blocked. The runtime persists both values through CrossPoint settings. Repeated generation against the verified Standard snapshot produces identical outputs: 76 supplied keys, seven book profiles, twelve recents, five remaining setting blockers, no omitted-default or recent-capacity blockers, and unchanged source digests. All 86 Python checks pass. Gallery enumeration order, unresolved progress records, contextual/annotation behavior and transactional activation still prevent treating the full migration as ready.

## Wi-Fi identity resolved (2026-10-02)

The supplied factory identity validates the CPV1 integrity checks of both networks in the captured Standard snapshot. `prepare_yacp_wifi_candidate.py` now produces a repeatable, verified candidate for that same reader in the private local archive. The existing source snapshot and original card are unchanged. Source and target identities are explicit; another reader needs a separately keyed candidate.

The production C++ obfuscation decoder and CRC code accept both converted records under address/undefined sanitizers with the host mbedcrypto library. They re-encode to identical candidate bytes. This verifies the credential format, not a physical Wi-Fi connection or the full store activation process. No plaintext password is written or logged. The candidate remains disabled, and the complete migration still has the blockers listed above. The earlier missing-source-MAC statements are superseded by this checkpoint.

## Bookmark conversion experiment

`yacp_bookmark_migration.py` retains the complete decoded v2 to v5 source record, including timestamps, chapter names, paragraph indices and snippets. It derives a CrossPoint visible-text offset from a unique saved snippet or the first line of the corresponding old cached page. The old paragraph lookup and float page calculation are reproduced for cache lookup. Only version-27 and version-44 section layouts are accepted. Missing or duplicate text blocks the whole book candidate; percentages alone are never used as an automatic destination.

The CrossPoint JSON filename is derived by its existing flattening rule. Distinct paths that collide under that rule, including FAT case aliases, must be resolved before activation. The candidate records this requirement and never overwrites an existing bookmark store. Summary/progress fields use CrossPoint's schema, with progress weighted by inflated spine bytes. Original metadata remains in the review record even when the CrossPoint UI has no matching field.

These are offline tools, not automatic on-device import. They allocate nothing on the ordinary reading path. The actual snapshot contains no old binary bookmarks; synthetic navigation tests therefore remain distinct from real-data validation. Large bookmark collections still require C3 memory acceptance before activation; the source format permits 1,024 entries and CrossPoint currently loads its list into memory.

## Stable counter conversion (candidate v23)

The optional `stablePageNumbers` behavior now passes real-manifest parsing and X3/Pro simulator comparisons. Candidate v23 was generated twice with identical output and unchanged source digests: 76 stored keys, four blocked global choices, seven book profiles, twelve recents, no omitted-default blockers and no shared-state field blockers. Gallery order and complete activation are still unverified. The remaining global choices are publisher page numbers and the three legacy long-button actions. Six book profiles still await publisher-number support at this checkpoint.

Standard 274 compiled successfully with the stable counter: application 6,394,607 bytes, static RAM 58,776 bytes, image 6,408,688 bytes, OTA headroom 144,912 bytes. The checked image SHA-256 is `72f56dfe2be5dc956b459e6e541aa92c6e4ac67aed745277758ecaf432476bf3`. This is a local test image, not a migrated or hardware-validated release.

## Publisher counter conversion (candidate v24)

Publisher numbering now passes X3/Pro layout comparisons, Pro per-book controls and old-cache reconstruction at the saved visible-text offset. Candidate v24 preserves all seven real profiles with no blocked book field. Production C++ profile loading and reset accept all seven under address/undefined sanitizers. Global conversion has three remaining blockers: legacy long Back, Menu and Power commands. Twelve recents and shared-state fields remain covered, but gallery order, unresolved book positions and full activation are still separate open items. Candidate files and originals remain outside the firmware tree and were not installed.


## Reader-command conversion candidate v25

All 76 real source keys now have accounted-for conversions or documented consumption. The three remaining command keys retain their original values and are active only in C3 EPUB reading. CrossPoint Home and touch are unchanged on S3. The generated candidate contains seven converted book profiles, twelve recents and the preserved shared state, with no global, default-field, recent-capacity or shared-field blockers. Clipping command IDs remain blocked for other source configurations until their runtime is implemented.

The candidate is not an installer. Five progress records still lack an accepted exact mapping or a decision about experimental copies. Gallery enumeration order has not been established, and transactional activation has not passed. `ready_to_activate` remains false. Source copies and source card are unchanged.

## Scope and source availability update

The user excluded nearby position exchange, selected text excerpts/export and global annotation access. Their implementation is no longer a migration-release gate; existing source data is still retained. A saved shortcut targeting an excluded clipping action remains an explicit review case rather than being silently mapped to a different action. The supplied snapshot has no such remaining blocked global key.

The source G: card is currently unavailable. Its earlier private verified copies remain accessible. Gallery enumeration order and four saved progress records remain unresolved after the mode-aware cache correction below; no assumption that these are disposable experiments has been made. The user has been asked to reconnect G: while independent work continues.

Font-manifest verification now compares entries by their stored relative paths, independently of WindowsPath versus PosixPath sorting. It still checks every size and SHA-256 and rejects changed content or duplicate entries. This does not claim that the original FAT directory order of sleep images has been recovered.

## Eleven reviewed positions verified on both simulators

The eleven exact candidates from anchor-candidates-v4 now open the containing page on X3 288 and Pro 287 with converted global settings, the real captured font catalogue and the matching converted book profile where present. On exit, the ten-byte progress records retain the page-start visible-text offset. Tests inspect each generated chapter's offset table and require the original anchor to fall inside the saved page. All source and copied EPUB hashes match their manifests.

The acceptance scripts preserve the user's custom front-button mapping rather than assuming default Confirm/Back keys. Two ending-book cases require cancelling the normal completion prompt before inspecting committed progress. Their initial navigation-test failures were harness assumptions, not an accepted position conversion. The reruns pass on both boards. Aggregate evidence: `qa-artifacts/real-positions-validated-288.json` (22 board/position cases). This does not resolve the five unmatched/incomplete candidates, gallery order, full migration activation or physical acceptance.

## Mode-aware cache correction and twelve validated positions

The earlier L'Homme Terminal blocker was an incomplete cache lookup: `48_balanced.bin` exists, has the saved thirteen-page count, and its mode matches the explicit per-book override. The frozen reader selects a suffixed cache only for that explicit override, independently of the text-settings snapshot. The offline anchor tool now follows that rule and rejects unknown or corrupt modes. It never tries an alternate engine's cache merely because the ordinary filename is missing. The target firmware continues to use CrossPoint layout.

Anchor candidate v5 identifies the unique source text at offset 2,603 in spine 48. X3 288 resumes the containing page at offset 2,091, and Pro 287 at 2,514. Both preserve the EPUB bytes. The aggregate `qa-artifacts/real-positions-validated-v5.json` now covers twelve positions on each board, with four unresolved source records. All 105 Python tests pass, including explicit engine selection, inactive snapshot modes and corrupt-profile rejection.

The old 32-bit Harcelement cache matches the historical root path of the EPUB now available under `/Read`. Its early progress is retained separately from the later completed-book progress; a path hash does not authorize merging them. The private `historical-root-path-hint.json` records the evidence. A read-only search of 520 local EPUB files (274 unique contents) in the original project and neighboring experiments found no package matching the other three orphan identities. Source-card inspection or a user decision is still required. No original, candidate from earlier experiments, or real card was overwritten.


## Final software check

Native Pro build 320 repeats the raw-card conversion checks (78 settings, 11 positions and five book profiles). Its touch opens Congo at the containing page, Home returns from reading, and the front-button setup wizard is absent. The source archive remains unchanged. All 118 existing Python tests pass. Aggregate evidence is `qa-artifacts/on-device-migration-acceptance.json`; the six aligned firmware builds are 314 through 319. The public-release gate remains physical validation, beginning with X3 Standard 314.
