# YACP on CrossPoint 1.6.5

Current source policy (2026-10-17): this CrossPoint tree is the single active YACP implementation for all four readers. See [source consolidation](yacp-source-consolidation.md) for recovery decisions, safeguards and current validation. Older source-selection notes are historical.

## Cover-free YACP Home restored (2026-10-16)

The follow-up now includes the contextual settings hints, Magic Flash/Display Power Saving renames and opt-in Magic Flash 2X in the actual CrossPoint tree. The first implementation in `YACP implementations 1` had modified the separate legacy checkout, so it never reached the previously installed port. All 34 locales are populated. The X3 method picker precedes its interval and retains saved values 0/1, with 2 added for the double pass. Other devices retain the preference without running the X3 waveform.

Native builds 490 (X3) and 491 (Pro) pass 24 EPUB/TXT scenarios spanning all three methods and smoothing on/off. Pixel outputs match the corresponding baseline; only X3 selects the second pass. Physical waveform quality and energy remain unmeasured. Method/interval save and cancellation pass on X3, and Pro touch checks confirm that help taps keep the dialog open and option taps save the interval. Standard 492 was installed on the X3; Pro 493 passes image validation.

An injected failure of the X3's 7,920-byte grayscale-strip allocation reproduced five skipped passes without BW completion on native 490. Native 496 passes the same five failures with BW completion, keeping the intact page and respecting display power saving. Standard 495, built while the other conversation completed its menu renames, includes this fix and the 40-pixel Home margins. The same X3 confirmed installation of 495 through `YACP implementations 1`; no post-reboot API poll or physical contrast/energy claim follows from that response.

Christophe explicitly approved returning to the original typographic Home after reviewing the later cover-based redesign. The YACP card now presents the title, author, cached progress, recorded reading time and saved remaining-time estimate. Rendering no longer opens cover BMP files or generates missing thumbnails. Selected-book metadata and statistics load on entry, selection or an explicit edit, with no recent-cache scan or global statistics load during ordinary painting.

Touch capability preserves the X4 Pro Statistics, Autonomy and Menu shortcuts, card resume and recent-book swipes. X3, original X4 and X4 Classic retain physical navigation and secondary-menu access; their card uses the space otherwise reserved for touch rows. The current reader exit restores portrait, which is the orientation captured in simulator acceptance.

Native simulator builds 483 (Pro) and 484 (X3) pass synthetic navigation, selected-book progress/time updates, unchanged statistics/cover files, and return-to-the-same-card checks. Long titles/authors, unknown metadata and empty recents were visually reviewed in English. Existing labels cover all 34 locales. Captures and the validation report are retained under `qa-artifacts/home-restoration-20261016/`. Device validation remains pending.

Firmware builds 485 (Standard), 486 (Pro) and 487 (Classic) compile and pass image identity/integrity checks. The X3 at `192.168.1.151` accepted installation of 485 and returned `Firmware installed, restarting`; this is not a post-reboot visual check. The subsequent spacing adjustment doubles the card's side margins from 20 to 40 pixels. Simulator 488 verifies the white bands and retains a reviewed capture under `qa-artifacts/home-narrow-488/`; Standard 489 compiles and passes image checks. Installation of 489 awaits the reader returning to File Transfer mode.

## X4 Pro Wi-Fi boundary audit (2026-10-03)

The X4 Pro card's two saved network names, encrypted password bytes and last connected network matched the private pre-update backup. The current file added CrossPoint's length and checksum fields. A later format audit proved that the unchanged bytes still contained YACP's eight-byte `CPV1` envelope: each stored entry decoded to 27 bytes, while its actual password was 19 bytes. CrossPoint accepted and re-saved the envelope as if it were the password. The X4 Pro connected after the user forgot the saved entry and entered the password again.

`WifiCredentialStore.cpp`, `ObfuscationUtils.cpp` and `HalNetwork.cpp` are unchanged from the CrossPoint base. On X4 Pro, `WifiSelectionActivity.cpp` now uses CrossPoint's scan, connect, failure and forget behavior. Its remaining source difference is an X3/C3 RTC date-sync condition. The earlier fixed access-point hint has been removed. YACP's TCP Don't Fragment hook is enabled from KOReader sync, not from ordinary Wi-Fi selection.

The nearby statistics option, device portal additions and staged YACP updater are product features outside the ordinary join-network path. The shared keyboard's minimum-length check applies to the YACP device-name editor; Wi-Fi password entry keeps the CrossPoint default of zero. Simulator checks cover saved-network fallback, manual-list access, long-press to forget and password entry. The user confirmed physical X4 Pro Wi-Fi connection after manual password entry; automatic conversion of the old saved credential remains unverified and defective on this mixed card.

The X3 source card has a YACP identity marker, so its complete migration calls the credential converter. A verified same-device X3 candidate removes the `CPV1` envelope and yields two 19-byte CrossPoint passwords. The X4 Pro card instead had native CrossPoint settings plus an unmarked leftover CrossInk settings file. The importer now runs an isolated, idempotent credential migration for that case before the CrossPoint credential store loads. It preserves the original file and does not change ordinary Wi-Fi selection. Pro simulator build 345 passes complete and interrupted mixed-store conversions while retaining a manually re-entered CrossPoint row byte for byte.

## X3 and X4 Pro user acceptance checkpoint (2026-10-03)

Standard build 323 now has positive user feedback for migrated Congo progression, YACP Home and refresh preferences. Christophe explicitly confirms all three follow-up scenarios: closing and reopening the book, sleep and wake, and progress/preferences retained across a restart. This closes those X3 scenarios; extended function checks, the recovery shortcut and other models remain open. The subsequent Pro Standard build 327 test received positive user feedback: everything appears to work correctly after the requested reading, touch, Home, frontlight and persistence checks. This is an initial session report, not full feature or endurance acceptance. Original X4 and X4 Classic hardware tests remain open. Its returned card exposed leftover CrossInk settings beside native CrossPoint settings, so the importer now checks YACP identity first. Native Pro build 325 preserves the native settings and progress on that real-card copy.

## Physical SD migration correction (2026-10-03)

The X3 trial of build 314 failed before writing the migration intent. SdFat rejects repeated `mkdir` calls while the old simulator accepted them. Every migration directory now uses `ensureDirectoryExists`. Native build 321, with the simulator directory contract aligned to SdFat, passes the returned-card checks (78 settings, 11 positions, five profiles) and interruption/collision recovery. See `docs/yacp-migration.md`. Hardware acceptance remains open; the earlier six-build matrix does not validate this correction on a device.

## Previous correction after the raw-SD X3 trial (2026-10-02)

Build 297 did not perform on-reader migration. Prepared-copy acceptance below is historical and must not be used to claim raw-card compatibility. The current firmware now runs a restartable importer before all settings/state loaders. `docs/yacp-migration.md` documents the transaction and native evidence: 78 settings, 11 identified positions, five profiles, all legacy bookmark versions and interruption recovery. YACP Home and fresh X3 refresh defaults are corrected. The existing files on G: remain unmodified. Corrected Standard test firmware 314 was added under its descriptive filename for manual installation. Physical validation of the new firmware is still pending.


Base: `93e98bb78702e29868a16a13b80c40e6b36ccdff`, with FreeInk SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`.
Reference: complete local YACP snapshot in `XTEInk/YACP-archives/2026-10-01-crosspoint165/reference-source`.
The working branch is `chore/sync-crosspoint`. No device installation is approved by a successful build alone.

## Current acceptance boundary

The three excluded workflows are not pending features. The retained functionality has software checkpoints below, including common book-move recovery and twelve real reading positions on each of the X3 and Pro simulators. This remains a reconstruction under validation, not an installable release.

A complete offline migration now passes on a new verified copy of the returned G: card. It preserves eleven active book positions, twelve recents, five matched profiles (all seven originals preserved), settings, statistics, autonomy and convertible credentials. X3 295 and Pro 296 pass complete-copy reader checks; all 118 Python tests pass. Four unidentified saved positions and other unknown or excluded records remain archived and reported, without guessed progress. Temporary sleep-image anti-repeat indices are reset while images and preferences are retained. This removes the old gallery-enumeration requirement. The original card is unchanged. See `docs/yacp-migration.md` for the current procedure and limitations.

After migration, hardware acceptance must cover X3, then X4 Pro, followed by the original X4 and X4 Classic. In particular, simulator checks cannot validate C3 heap pressure, physical SD timing, deep sleep, display waveforms, radio behavior, touch or frontlight. Final Home visual acceptance also remains open. Use expendable copies for interrupted-write tests; keep the original card unchanged.

## Decisions and acceptance scenarios

Each row preserves the current observable YACP behavior unless CrossPoint already covers it. A source-level difference alone is not a reason to replace CrossPoint code. `Pending` is not a claim of equivalence or completion.

## Scope confirmed by the user (2026-10-02)

Do not implement nearby reading-position exchange, selected text clippings/export, or cross-book annotation access in this reconstruction. These inherited CrossInk workflows are excluded, not unfinished acceptance gates. Keep CrossPoint KOSync, its bookmarks with text summaries and names, and its per-book bookmark interface. Original saved data remains preserved in the private archive. Nearby statistics exchange remains included because statistics were explicitly retained.

Reuse CrossPoint book deletion, file management and removal from recents. Additional duplicate shortcuts are not a completion requirement. Remaining integration work concerns the retained statistics/history, book profiles, galleries and data safety across those existing actions.

| ID | Behavior and source | Decision | Acceptance scenario | State |
| --- | --- | --- | --- | --- |
| F01 | Lexend Deca and Bitter, four styles | Generate using current CrossPoint font format and converter | Accents, ligatures, symbols, bold and italic match selected family | Standard Pro 249/X3 250 and XLarge Pro 253/X3 254 pass 28 family/size scenarios with four faces, accents and saved page-turn progress; screenshots reviewed; shared 16 pt pixels/progress match; hardware pending |
| F02 | Standard 10/12/14/16 pt, XLarge 16/18/20 pt | Keep both for C3 and S3; keep CrossPoint SD/vector fonts | All offered sizes render; image fits OTA slot | All six aligned firmware builds pass (297 to 302), with embedded identity, image checksum and SHA-256 verified; each fits its OTA slot. Hardware acceptance remains pending |
| F03 | EPUB layout, justification, CSS and hyphenation | Keep CrossPoint implementation | Same EPUB/settings as stock CrossPoint; photo passage included when available | Pristine CrossPoint and Pro 212 produce identical pixels for the photographed Congo passage at matching settings; eight generated pages have identical geometry |
| F04 | UI, touch, Home key and reader menus | Keep CrossPoint defaults and components | Home returns home; long press opens reader menu; touch and buttons work | Upstream retained |
| F05 | YACP single-book Home and recent switcher | Port as default Home; retain upstream themes | Previous/next valid recent book, resume, no covers or full-library scan | X3 and Pro navigation passed; selected-book statistics, inclusive progress and cached time-left verified; final visual parity pending |
| F06 | Lazy Home menu data | Preserve | OPDS and global stats loaded only when requested | Source review: OPDS availability loads on menu entry; global totals only on explicit statistics action |
| F07 | Side-button long press and optional previous page | Port specific YACP choices through logical input | One hold, one page; orientation and release suppression work | Independent side long-press modes on X3, original X4, X4 Classic and X4 Pro; nine X3 363, nine X4 375, eight Pro 353 and nine Classic 354 simulator scenarios passed; physical validation pending |
| F08 | Quick Resume after automatic sleep | Keep upstream support, add only missing YACP fast path/defaults | Cached wake retains page; stale cache takes safe fallback | CrossPoint already retains the visible sleep frame, restores Quick Resume and reseeds X3 controller RAM; YACP Quick Resume defaults restored; hardware validation pending |
| F09 | X3 boot delays, USB deferral, Safe Home and network boot chords | Keep CrossPoint asynchronous release logging; add physical C3 chords | Battery wake, USB wake, held buttons and recovery all work | Physical Back/Up/Down routes implemented with two stable ADC samples; Tiny 210 compiled; Pro 211 saved-network fallback passed; physical chord and power validation pending |
| F10 | X3 low-frequency idle and edge sampling | Compare upstream power manager before adapting | No dropped short presses, no latch loss, idle current recorded | CrossPoint already uses C3 10 MHz idle and ten-ms raw-input checks; X3 gauge USB reads now paced at one second as in YACP; hardware current and short-press validation pending |
| F11 | X3 no-flash refresh and AA behavior | Retain upstream SDK waveforms; add bounded periodic text reinforcement | Text-only AA, image pages, manual cleanup, both X3 controllers | X3 231 and Pro 232 EPUB/TXT checks pass with unchanged pixels; required cleanup and non-X3 gates retained; manual reinforcement shortcut verified in F52; physical controller checks pending |
| F12 | Display power-down and image wake | Preserve useful YACP behavior within SDK capabilities | First page after wake is correct on each controller | Existing CrossPoint turnOffScreen/deepSleep retained; fadingFix migration verified; hardware pending |
| F13 | Debounced EPUB/TXT/XTC progress writes | Add only missing debounce; flush on reader exit/sleep | Forward/backward turns, exit, sleep and restart retain position | Debounced checkpoints retained; X3 295 verifies TXT/XTC exit/reopen, TXT six-byte content anchors after repagination and four-byte backward compatibility; hardware pending |
| F14 | EPUB prefetch and bounded low-memory recovery | Keep upstream layout; compare work, memory and recovery before port | Large CJK chapter, chapter boundary, direct final-page jump | CJK cold last-page jump and cached reopen pass X3 216 and Pro 217; pristine CrossPoint Pro has identical 40-page geometry and final image; real C3 heap faults and prefetch latency remain pending |
| F15 | Lazy SD-font discovery and compact Unicode metadata | Defer the catalogue for built-ins; keep CrossPoint script fallbacks | Built-in reading needs no SD font scan; CJK font fallback preserves choice | Lazy catalogue and missing/corrupt-family preference preservation verified on Pro; CJK Home metadata fixed and reviewed on Pro 184; real C3 heap coverage pending |
| F16 | Book reading statistics and first completion dates | Port current YACP formats and accumulation | Timed session, pause, reread and manual date edit | Codecs, EPUB hooks, previews and date edits verified in host tests and Pro simulator; hardware pending |
| F17 | Global and synchronized reading totals | Port current YACP formats | Restart, device totals, imported totals without double-counting | Codecs and local-device exclusion tested; X3 250/Pro 249 show identical three-hour totals with and without temporary/backup duplicates; physical nearby exchange pending |
| F18 | Daily/weekly Reading Rhythm and streaks | Port current 16-bit daily-history format | Reading day longer than 255 minutes, year rollover, no RTC | Daily buckets and charts verified on X3 and Pro; real hardware pending |
| F19 | Reading Achievement and 32-book completed index | Port current YACP behavior | Real ending completes once; final-page exit asks; dates survive reread | Automatic completion, index/history, one-time achievement and confirm/cancel flows verified on Pro; header spacing corrected and visually verified; hardware pending |
| F20 | Statistics backup, restore and nearby synchronization | Preserve source formats and workflows | Backup/recover from copy; duplicate sync does not add totals twice | Backup/recovery pass 60 host cases including seven real archives; nearby protocol, interrupted snapshot writes and duplicate aggregation pass host checks; Pro 249/X3 250 entry and Back pass; physical radio/SD validation pending |
| F21 | Battery history, displayed-page count and current indicator | Port without per-page writes or new periodic tasks | Charge cycle, both page directions, sleep, USB edge and no-RTC board | History codec, fixed counters, EPUB/TXT/XTC hooks and CrossPoint UI ported; nine host scenarios and real state round-trip passed; Pro and X3 UI verified; current indicator ported from the cached X3 sample; hardware pending |
| F22 | Adaptive sleep images and cached grayscale planes | Compare new upstream grayscale; retain YACP selectable behavior | Same BMP, very small image, cache reuse, orientation and source change | Adaptive filter ported; 11 host comparisons and X3 175 UI passed; three-plane cache passes Pro 220 and X3 221 cold/warm, corrupt-cache and same-size-source scenarios; hardware pending |
| F23 | Per-book galleries and no-repeat rotation | Port paths and selection behavior | Two books with separate galleries; global fallback; no immediate repeat | Bounded per-book selector and X3 simulator 173 passed; global CrossPoint rotation retained; hardware pending |
| F24 | Browser crop/resize, thresholds and sleep-image manager | Adapt to upstream portal and upload APIs | Prepare BMP, upload to explicit selected gallery, preview and cycle sleep images | Native BMP decode host checks and X3 241/Pro 242 HTTP/browser acceptance pass; physical upload/sleep acceptance pending |
| F25 | Wi-Fi saved-network fallback and scan reuse | Keep CrossPoint connection, fallback and list unchanged; remove the YACP fixed AP hint | Missing strongest SSID; next candidate succeeds; Show interrupts | Fallback and Show verified on Pro simulator; fixed AP hint removed after hardware connection failure, real radio validation pending |
| F26 | KOReader Smart Sync and safe comparison fallback | Keep CrossPoint content mapper; retain YACP opt-in and manual equal/uncertain fallback | Local ahead, remote ahead, missing/equal/invalid remote position | Policy and timer host tests passed; local-server equality/unknown scenarios perform no upload and show comparison on Pro; hardware pending |
| F27 | TCP Don't Fragment and temporary Wi-Fi power policy | Keep CrossPoint's sync power policy; scope YACP station-output hook to KOReader sync | KOReader requests succeed; ordinary Wi-Fi connection unchanged; previous Wi-Fi policy restored on errors | Header/checksum host tests and Standard 162 passed; real network validation pending |
| F28 | Staged OTA, release matching and remote SD install API | Keep upstream transport where equivalent; add YACP asset selection/API | Hash mismatch, wrong board/variant, interrupted download, valid image | Staged OTA host tests and Standard 169 passed; Web API contract verified on Pro 170; hardware pending |
| F29 | Dictionary | Keep CrossPoint engine and interface | Lookup on demand; inactive reading path does not scan dictionaries | CrossPoint retained; explicit lookup/return and no automatic indexing verified on Pro with a synthetic StarDict |
| F30 | CJK metadata, UTF-8 composition and bitmap alignment fixes | Keep equivalent upstream fixes | Decomposed accents, CJK title, non-byte-aligned bitmap | 847 composition pairs passed against both implementations; upstream Hangul/in-place handling retained; 11 bitmap fixtures passed; CJK Home 184 visually verified |
| F31 | Existing bookmarks, clippings, recents and saved items | Preserve original data; use CrossPoint bookmarks and the retained recent-book interface; omit excluded annotation workflows | Import reference SD copy and reopen every retained stored item | Bookmark v2 to v5 candidates navigate correctly on X3 269 and Pro 270, including 16-point repagination; real snapshot contains no binary annotations; eighteen-entry recents restored; clipping runtime and global annotation access excluded by user; complete offline copy tested on X3 295/Pro 296; physical acceptance pending |
| F32 | OPDS, Calibre, file browser, USB MSC and TTF | Keep CrossPoint functionality | Basic open/transfer flows on supported hardware | Upstream retained |
| F33 | Settings and state migration | Explicit origin-aware import before ordinary state writes | Two migrations identical; interrupted migration retry; source preserved | Complete offline copy preserves origin-aware settings/state, with interruption/retry tests and production state round-trip; X3 295/Pro 296 verify 79 converted settings and 25 protected stores. Only temporary sleep indices reset; installation and physical acceptance pending |
| F34 | Progress/cache migration | Keep user position independently from rebuilt layout caches | All formats resume at matching content after cache rebuild | Twelve prior records cover eleven unique books; the verified source FNV64 policy selects the active Alice cache. Complete copy retains eleven active positions; X3 295/Pro 296 resume Congo with converted profiles. Four unidentified positions archived without guesses; hardware pending |
| F35 | Brand, documentation, source artwork and credits | Keep canonical useful YACP assets; public website deferred | Docs describe actual build and migration; original authors retained | Canonical assets preserved; X3 245 and Pro 247 sleep reviewed, five actual portal pages and SVG/ETag pass; physical boot/sleep acceptance pending |
| F36 | Hardware support | Keep CrossPoint X3/X4/X4C/X4Pro profiles | X3 first, then Pro, original X4 and Classic; SD/sleep/touch/frontlight | Upstream retained, hardware pending |
| F37 | Numeric line height, paragraph indents | Preserve optional controls through CrossPoint's render specification | Changing a setting retains content position; default layout stays CrossPoint | Numeric line height, preset reset and optional paragraph indents verified on Pro; hardware pending |
| F38 | Bionic reading | Use CrossPoint Focus Reading for the equivalent word-prefix emphasis | Existing enabled preference maps to Focus Reading; punctuation and hyphenation remain upstream | Upstream equivalent retained; the bionicReadingEnabled preference maps to focusReadingEnabled in migration candidates; hardware verification pending |
| F39 | Guide reading with inter-word dots | Decorate CrossPoint's existing gaps, retaining its layout | Off gives prior output; on preserves text, positions, links and offsets | Pro 212: five pages and 300 dots preserve geometry; off is pixel-identical to 211; SD font four-page comparison and book-scoped UI toggle pass; X3 216 four pages and 297 dots pass against 207; hardware pending |
| F40 | Stable page numbering and publisher page numbers | Port missing numbering, separately from page layout | Reference manifest ranges resolve; publisher anchors follow text; old caches retain the saved passage | Stable manifest tests and X3 272/Pro 273 pass; publisher labels, per-book scope and section upgrade pass X3 277/Pro 278; seven real profiles accepted; hardware pending |
| F41 | Status-bar time left, progress-bar position and current draw | Add missing choices to CrossPoint status-bar settings | Toggle each field; orientation and narrow screens remain legible | Implemented and portrait controls verified; software SDL resolves capture clipping, with X3 241/Pro 242 landscape EPUB and Pro TXT screenshots reviewed; XTC retains upstream portrait and overlay placement; physical current/clock and hardware pending |
| F42 | Reader-only dark mode | Keep separate from CrossPoint's global night mode | Reader can invert without changing menus or sleep images | Pro reader-only/global modes and settings toggle verified; X3 normal/dark Quick Resume verified; hardware pending |
| F43 | Clock visibility and saved date state | Map to CrossPoint clock/header settings and RTC timezone handling | Correct local date/time, no duplicate UTC offset, no-RTC fallback | Date trust, X3 Home date, full NTP flags and autonomy gate ported; host and X3 177 checks passed; hardware pending |
| F44 | File extension visibility and one/two-line browser rows | Compare current browser before retaining missing choices | Same library, long title, touch row selection | Pro one/two-line rows, hidden extension and touch opening verified; default output matches upstream component rendering; hardware pending |
| F45 | Reading-only front-button mapping and orientation-aware side buttons | Retain optional C3 behavior; leave Pro Home defaults to CrossPoint | Different reader/menu maps, both orientations and long presses | Reader-only four-button profile ported; X3 179 assignment/cancel/reset and invalid/partial import verified; Pro 191 Home/touch and side actions unchanged; 24 X3 190 orientation/profile cases passed; hardware pending |
| F46 | Named device and statistics auto-backup | Preserve for nearby statistics and maintenance | Distinct device identities, backup before reset, no hot-path work | Device-name API and KOReader payload verified on Pro 226; Save/Cancel via X3 227 buttons and Pro touch; names retained in nearby protocol; automatic backup covered by F20; hardware pending |
| F48 | Nearby book-position exchange inherited from CrossInk | Excluded by user; keep CrossPoint KOSync | No new proximity-position workflow | Excluded, not a release blocker |
| F47 | OPDS credentials and server storage | Preserve per-server download naming; convert legacy credentials with explicit keys | Login to same server without exposing credentials | Per-server choices, global inheritance, actual store codec and failed-save rollback pass; X3 235 Save/Cancel and Pro 234 API/reload pass; offline conversion covered by complete-copy tests; external authentication and hardware pending |
| F49 | Tilt direction and reader-only sensor power | Retain CrossPoint normal/inverted modes; add YACP forward/back modes through the SDK | Four modes in four orientations; off preference preserved; no sensor reads outside reading; failed transitions retried | Host HAL and migration checks passed; seven X3 197 settings scenarios and visual review passed; Tiny 196 compiled; hardware pending |
| F50 | Per-book text settings and automatic page-turn interval | Preserve book scope with CrossPoint text controls; archive old experimental rendering-engine choices | Two books retain distinct choices; global defaults remain unchanged; old font indices converted using the captured family catalogue | Book scope passes host faults and X3 203 two-book, real-profile and Quick Resume scenarios; Pro 206 real-profile and text controls pass; custom interval save/cancel/reopen passes X3 207 and Pro 208; complete offline copy includes five matched profiles and preserves two unmatched originals; hardware pending |
| F51 | Separate recent-books list/grid choice (`recentBooksView`) | Compare with the port's recent list and CrossPoint cover-grid components before adding the missing view | All 18 recents reachable, selected book retained, missing files skipped, no covers loaded for the ordinary YACP Home | Dedicated list/grid and persisted preference pass X3 257/Pro 258; candidate v22 maps the preference. X3 260/Pro 261 add open/statistics/reset/remove actions, including touch hold. Cache maintenance passes 19 fault/retry cases. Duplicate delete shortcuts are excluded; existing CrossPoint operations and retained statistics use the shared book-move recovery service. Hardware remains pending |
| F52 | C3 long Back/Menu/Power commands (`longPressBackAction`, `longPressMenuAction`, `longPwrBtn`) | Preserve distinct commands using existing reader actions; keep Pro Home and touch defaults | Each hold triggers once, release is consumed, wake and screenshot chords retain precedence, unsupported formats are explicit | C3 EPUB dispatch: 24 command cases, six guard scenarios and five scrolling-list settings cases pass across 280/283; Pro 281 Home/touch isolation passes. Tiny 284 succeeded. Clipping dispatch excluded; hardware pending |

## Verification rules

- Increment `crosspoint.crossink_version` before every firmware build, including retries.
- Build `tiny` after each implementation block; build `xlarge`, `x4pro`, `x4pro-xlarge`, `x4c` and `x4c-xlarge` before handoff.
- Record build size, version, variant and test outcomes. Never label untested hardware as validated.
- Simulator captures use English. A simulator result cannot establish SD timing, power, grayscale or deep-sleep correctness.
- Migration operates first on copies. Existing statistics and progress are not disposable layout caches.
- The original YACP checkout and the interrupted CrossInk worktree remain references. No commit or publication without an explicit request.

## Settings coverage

`docs/yacp-settings-audit.json`, generated by `scripts/audit_yacp_settings.py`, inventories all 85 scalar settings declared by the frozen YACP header. It compares the published release, the local snapshot, pristine CrossPoint 1.6.5 and the current port. Conditional font-variant defaults are retained separately. It is an omission check, not proof that identically named settings have the same meaning. Function-level work outside this settings header remains covered by the behavior matrix and source audit.

## Statistics implementation boundaries

`ReadingSession` is allocated once per open EPUB with a fallible allocation. Its fixed daily-history storage stays off the small task stacks. Page timing performs no allocation or SD write. The render task publishes a completed page through atomics; the input task owns all session counters. Covered activities, reader overlays and footnote navigation pause timing. Stats are saved on reader exit. CrossPoint's Home key and reader gestures are unchanged.

YACP's current thresholds are retained: forward-page dwell of at least two seconds, idle intervals rejected in full, ten seconds for accumulated reading time, sixty seconds for a session, and the first eligible forward interval excluded from pace. Chapter endings do not train pace. Reading-start date qualification and manual overrides are preserved. Like current YACP, time buckets attribute accumulated active duration from the session's starting local time; reconstructing the exact placement of long pauses would be a separate behavioral change.

The settings include tracking on/off and the idle threshold, with all 34 language files updated. The interval picker uses CrossPoint's existing component. Automatic completion and the 32-entry finished-book index are now wired to a forward turn past the final chapter. The statistics screens and explicit date editor are integrated into CrossPoint menus. Completion prompts and the achievement screen are verified in the Pro simulator. Backup/sync interfaces remain pending. Live EPUB time-left and its cached Home display are implemented.

Pro simulator version `1.7.2-yacp.120` verified two timed scenarios. A 20-second reader overlay was excluded from the 42-second accumulated reading time. A separate 10-second frontlight-panel visit was excluded from the 18-second accumulated time, exercising activity cover/uncover notifications. Both returned via the normal Home action. These scenarios use isolated English fixtures, not the real card.

## Build commands

Use `python scripts/build_yacp.py tiny --pio <platformio-executable>` for Windows firmware builds. The wrapper increments the independent YACP test counter before invoking PlatformIO, forces UTF-8 for multilingual generator output, and preserves successful binaries with their version, target and SHA-256 under ignored `qa-artifacts/bin/`. It never flashes or publishes.

The wrapper now verifies the actual ESP descriptor, chip ID, board tag, segment checksum, appended SHA-256 and OTA slot size before preserving a firmware. It also saves the ELF for size/crash analysis. Standard 192, XLarge 193, Pro Standard 205 and Pro XLarge 195 passed this read-only image check. The Pro Standard 205 image leaves only 25,152 bytes in the unchanged CrossPoint OTA slot, so subsequent blocks must keep checking flash capacity. This does not validate hardware or authorize installation.

For the existing WSL mirror, use `python scripts/build_yacp.py simulator_x4_pro --wsl-mirror /home/christophe/yacp-builds/crosspoint165-111`. The same version gate runs before copying sources and invoking the native compiler. It excludes the simulator-patched GfxRenderer source from mirror synchronization.

The simulator is configured in `platformio.simulator.ini`, pinned to commit `20e738038605ff4ead6c9fcfe932443d1e143691`. Native builds run under WSL. The Linux mirror at `/home/christophe/yacp-builds/crosspoint165-111` is a disposable compilation copy; the Windows worktree remains canonical. Its PlatformIO `.pio-link` metadata must refer to the Linux mirror, otherwise dependency scans still traverse the Windows drive. The current simulator works with `lib_ldf_mode = chain+` and `-Dmemcpy_P=memcpy` for AnimatedGIF.

For headless screenshots, use `SDL_VIDEODRIVER=offscreen`. SDL's `dummy` driver accepts scripted inputs here but does not provide the accelerated renderer needed for screenshot capture. Fixtures use English and copied books, without real network credentials.

## Verified statistics UI and reference passage

Standard build `1.7.2-yacp.126` passed with 6,208,855 bytes of application flash (94.7% of its partition) and 57,936 bytes of static RAM. Pro simulator `1.7.2-yacp.127` passed reader-to-stats navigation, date editing, return to the same page and Home. Nineteen active reading seconds were retained once, with one manually completed book and no sixty-second session. X3 simulator `1.7.2-yacp.128` verified physical-button navigation through totals, rhythm charts and finished-book history. All new statistics labels are present in all 34 languages, with matching format parameters.

The explicit stats action releases the section's pagination buffers using CrossPoint's existing chapter-list pattern. It retains the content offset for return. The gain is a reduced overlap between live pagination buffers and the statistics activity, not a measured reduction in reading latency or hardware power. Pro simulator `1.7.2-yacp.129` repeats the editing/resume scenario after this change and still records nineteen seconds once. Landscape statistics layout, other UI scales, no-RTC layout and hardware heap headroom remain to validate.

A private fixture from the copied card loaded Atkinson Hyperlegible Next 16 pt in its four styles and resumed Congo at spine 22, offset zero. This is the photographed passage, "Dans le cercueil". The title is centered and the text does not reproduce the photo's large inter-word gaps under the captured Standard settings. This does not establish that the photographed CrossInk settings were identical. A second X3 fixture resumes the Ackroyd book at spine 19, page 2, containing the recovered offset 1059. CrossPoint reports page start 1047, and visual inspection confirms the recovered words occur on that page.

## Time-left port

`TimeLeftEstimate` retains the YACP pace thresholds and the 90% progress-based floor. It uses CrossPoint's resident cumulative chapter sizes and its estimated page count for partially built chapters. No file is opened by the estimator. Two scalar atomic snapshots carry pace and accumulated time from the input task to the render task, avoiding concurrent access to the mutable session. The label is optional, defaults to hidden, and uses CrossPoint's existing status-bar settings component. Only reader exit persists the cached book estimate; Home loads that value for the selected book.

Pro simulator `1.7.2-yacp.131` verified an existing 30-second pace producing a 60-second book estimate, its status-bar rendering, and the same value on Home. The text panel also correctly labels the built-in family Lexend Deca. Sixteen host statistics scenarios now cover arithmetic overflow and persistence of the estimate. A hardware timing/power measurement is still required; simulator throughput is not device performance.

## Autonomy port

`PowerHistory` keeps the existing 21-point state format. It accumulates awake battery time without a periodic task and samples battery only at the existing sleep boundary, reusing the last valid HAL sample when available. Charge detection retains YACP's observed two-percent and inferred eight-percent thresholds. The MCU counter wrap and integer saturation are tested. The HAL now distinguishes an unavailable battery sample from a genuine zero-percent value.

The renderer publishes only a fixed atomic page counter. The input task updates persisted state; the autonomy screen takes a fixed snapshot on entry. No page hook opens a file or reads battery telemetry. The screen is available from YACP Home and System settings, with CrossPoint touch buttons. All nine labels are present in all 34 languages. The allocation is an explicitly opened activity owned by ActivityManager, with no chart vectors or per-page heap allocation.

The host test compiles the actual power-history and state JSON code against deterministic time/battery facades. Nine scenarios plus the supplied state's autonomy round-trip passed. Pro simulator `1.7.2-yacp.134` opened the copied history and returned through the touch footer without altering its data. The simulator always reports USB connected, so battery discharge, page counting on battery and deep-sleep electrical behavior still require hardware. At least one actual battery sleep, a forward/backward page sequence and a USB charge/unplug cycle must be checked on X3 and then Pro.

Standard `1.7.2-yacp.132` passed before the autonomy block: 6,212,693 bytes application flash and 57,960 bytes static RAM. The remaining partition headroom must be checked at each later port; this build does not validate the final feature set.

The X3 simulator `1.7.2-yacp.136` verified autonomy entry through physical buttons and return, using the copied real history. The page counter and its USB gate share one atomic word so a mode transition cannot split a counter publication. Default sleep mode and timeout behavior now select Quick Resume, as in current YACP, while retaining CrossPoint's wake/display code. The X3 USB gauge read is limited to once per second in `HalGPIO::update`; `inputMgr.update` still runs every loop. This reduces direct I2C transactions, with up to one second of USB edge latency. It is not a measured battery-life improvement.

Standard `1.7.2-yacp.135` passed with the autonomy UI: 6,230,261 bytes application flash and 58,080 bytes static RAM. Firmware file SHA-256: `3ba75134491e51e075168734b583925b7bf3dd90277946bd9827fa93b84502ef`.

## Progress checkpoint implementation

`ProgressCheckpoint` holds at most ten serialized bytes and no heap allocation. The first displayed position is saved immediately. Later positions follow YACP's policy of ten position changes or five minutes observed on a changed position. Exit and sleep flush the pending bytes. Explicit synchronization saves remain immediate. EPUB visible-text offsets and TXT/XTC encodings stay CrossPoint's formats; section buffers can be released without losing the pending snapshot.

Host tests cover grouped forward/backward changes, identical repaints, metadata-only changes, five-minute checkpoints, timer wrap, retry after failed writes, exact ten-byte offset preservation and four-byte TXT/XTC records. This reduces progress-file writes by mechanism; device energy and SD latency have not been measured. Like YACP, an unexpected power loss between checkpoints can revert unflushed page changes. The existing CrossPoint temporary-file transport is retained.

X3 simulator `1.7.2-yacp.138` exercised a page turn with a pending checkpoint, sleep and a fresh-process wake. Logs show the second chapter was saved at sleep, then reloaded at spine 1, page 0 with its text offset. Before/after screenshots match the chapter. The retained framebuffer was saved and consumed on wake. Exactly one autonomy sleep sample survived. The simulator does not process scheduled screenshots during its sleep loop, so the test captures the raw retained frame and the before/after reader images instead.

The 5,747 preserved source files were rehashed against the snapshot manifest after the autonomy block. Both the original YACP checkout and the frozen reference still match every hash.

## Completion flow

Pro simulator `1.7.2-yacp.141` passed three independent fixtures: crossing the end and reopening without duplicate completion (one book, three forward turns, fifteen active seconds); confirming at the final page (one book, twelve active seconds); and cancelling then reopening without a repeated prompt (zero finished books, twelve active seconds). Prompts and summaries exclude their time from reading. CrossPoint's Home gesture dispatch and confirmation dialog are used; no long-press completion shortcut was added. Pagination is released before allocating the explicit summary. A future or unreadable book-statistics record blocks reconstructed session totals and completion changes.

Pro simulator `1.7.2-yacp.143` verifies the header-relative achievement layout and repeats completion/reopen without duplicate totals (one book, sixteen seconds). A separate two-book fixture selects the second card and opens that book's statistics. Home and statistics both display 100% on its final page; returning retains the selected card and every pre-existing binary file stays unchanged. Only YACP Home opts into the current-page-inclusive calculation; other CrossPoint callers retain their original calculation. Standalone date edits also update the canonical finished-book index after a successful book save. Hardware and compact-landscape visual coverage remain pending.

The TXT and XTC X3 fixtures both retained a pending page 1 through a forward/forward/back sequence. There were two progress writes: the first display and exit. A separate process reopened page 1. Standard `1.7.2-yacp.139` passed with 6,231,455 bytes application flash and 58,088 bytes static RAM.

Standard `1.7.2-yacp.142` passed before the final Home/achievement layout adjustment: 6,233,801 bytes application flash, 58,088 bytes static RAM, binary SHA-256 `4874e7eead1d6f35d0296598e0724ca32408211d6e0bd2f546338caf296da6b2`.

## Network comparison

CrossPoint `WifiSelectionActivity.cpp` already retries saved networks after the last SSID fails, sorts scanned saved SSIDs by signal, and lets Show interrupt automatic connection. Pro simulator `1.7.2-yacp.145` passed missing-SSID fallback and Show interruption. The YACP delta retains channel/BSSID alongside the existing strongest-per-SSID scan row and uses them in the installed Arduino `WiFi.begin` overload. Seven fixed bytes per row add no separate allocation. A missing hint keeps CrossPoint's full scan. The simulator does not expose per-result BSSID/channel and deliberately cannot validate this radio path.

CrossPoint `ProgressComparison.cpp` orders resolved content before percentages. That implementation remains intact. `YacpSmartSync.h` adds the YACP policy around it: Smart is opt-in; equal positions or unmapped estimates show the existing comparison UI; an automatic transfer requires usable content positions. The delayed return after upload is enabled only in Smart mode and uses unsigned elapsed time, including wrap and start at zero. No page-turn work or allocation is introduced. A host harness verifies content ordering, manual fallbacks and the timer.

CrossPoint already disables modem sleep for the sync activity and then tears down/restarts Wi-Fi on exit. That covers the short-request awake-radio intent of YACP's guard. The TCP Don't Fragment hook remains a separate pending HAL change, with packet behavior to validate on hardware.

Standard `1.7.2-yacp.144` passed the Home and achievement changes: 6,234,261 application-flash bytes, 58,088 static-RAM bytes, binary SHA-256 `3d87e2c53036a4957a3083f95b2f1e0841bd8aa3507e1304ab04cd8974284c38`.

Pro simulator `1.7.2-yacp.145` used a loopback-only synthetic KOSync server for two scenarios. Both equality and an invalid remote anchor at 99% requested the two document IDs, displayed the comparison, and sent zero uploads. The first UI script attempt selected the adjacent cache action in its disposable fixture; the corrected script and fresh fixtures exercise Sync Progress. No real card or external sync service was involved.

Standard `1.7.2-yacp.146` passed the network changes: 6,235,175 application-flash bytes, 58,088 static-RAM bytes, binary SHA-256 `c9a1868d9fac368e5d067004bd029bb13e3af3f8cc7fa906e3531394bc5b83cc`.

## Deferred font catalogue

`SdCardFontSystem::begin` now registers the resolver without scanning when a built-in family is active. Catalogue access from settings/installers/web still discovers fonts on demand. An explicit SD selection continues to load at startup for CrossPoint's UI script fallbacks. The active-font reload flag is separate from the catalogue dirty flag, so listing an updated catalogue cannot consume the reload needed by the next reader entry. Bitmap load failure keeps the saved SD selection, matching YACP and CrossPoint's existing vector-font retry behavior.

Pro simulator `1.7.2-yacp.147` verified zero catalogue scans during built-in startup/reading with a populated copied font directory, followed by exactly one scan on entering settings. The real copied Atkinson 16 pt selection still renders the photographed Congo passage at its original chapter anchor. A deliberately invalid copied 16 pt font retains the selection and renders the built-in fallback. The mechanisms avoid unrequested directory walks and catalogue allocations; no hardware boot-time, energy or CJK heap improvement is claimed.


## Numeric line height

The optional 70..200% value feeds CrossPoint's existing line-compression field. Zero keeps its four family-specific presets unchanged. The current section header already validates that float, so its binary format is unchanged. Both Text Settings and the live reader panel offer Custom through CrossPoint's interval slider. The scalar is stored separately; choosing a preset, including through the web API, clears it. The web portal does not yet expose the numeric slider. A fallible activity allocation occurs only when requested; no new page-turn allocation is introduced.

Pro simulator `1.7.2-yacp.150` passed baseline, 111% custom, cancelled adjustment, reset to Normal, and 101% through full Text Settings. Each selected page contains the previous visible-text anchor. Cancellation leaves the exact persisted position unchanged. CrossPoint anchors changes to the current page start, so repeated layout changes can move the top line backward; returning to a preset does not guarantee the original page number. The same upstream re-pagination path remains in use. English screenshots verify both slider surfaces without the previous option dialog bleeding through.

Standard `1.7.2-yacp.148` passed the deferred-font block: 6,235,137 application-flash bytes, 58,088 static-RAM bytes, binary SHA-256 `0349526d5e38a3a65a7a986f1b8b4d4190604754839a6ba1901e435d944e1c65`.


## Settings conversion candidates

The snapshot-only converter explicitly translates changed enums, font indices, YACP's legacy line-height scale, clock visibility and fixed UTC offsets. It prefers `crossink-settings.json` even when an old `settings.json` is present. Unknown, unsupported and invalid values remain named blockers; original bytes stay in the SHA-verified snapshot. Candidate files are not named `settings.json` and are not activated on a reader. Forty-three Python tests passed before the section-49 test was added, including interruption, repeatability, conflict refusal and source-hash verification.

The supplied Standard snapshot has 76 saved keys. Forty-seven are converted, consumed or superseded; twenty-nine remain pending. A private Pro `1.7.2-yacp.150` experiment loaded all 49 generated candidate values, with English and disabled tracking as test overrides, then read the reference Congo passage and saved without changing any of those values. This is settings codec coverage, not full migration acceptance.

## Dictionary and public assets

The existing CrossPoint dictionary code remains unchanged. Pro `1.7.2-yacp.150` synthetic StarDict fixtures verify that normal reading creates no dictionary index, while explicit Lookup followed by a word tap builds the one-entry index, displays its definition and returns to reading. Dictionary source files keep their hashes. This is not a compressed-dictionary or low-memory stress test.

`assets/brand/` retains the YACP logo geometry, public SVG/PNG and provenance. Its generator writes only that directory. `docs/yacp-project-layout.md` distinguishes canonical source, public material, generated outputs and the private external archive. Upstream documentation and credits remain intact.

Standard `1.7.2-yacp.151` passed the numeric-line-height block: 6,236,709 application-flash bytes, 58,088 static-RAM bytes, binary SHA-256 `0a67b55f51e1bfa86203c41ee9ee2a8681185c9a570e2ccb16ca29804054f246`.

## Optional paragraph indents

An off-by-default flag enters `ReaderRenderSpec`, the parser and `ParsedText`. It adds a one-em indent only to natural-aligned paragraphs without a nonzero indent, and permits existing positive indents with paragraph spacing. Hanging indents, centered/right-aligned text and list blocks retain their behavior. No per-page allocation is added. CrossPoint's line breaking, glyph rendering and justification remain unchanged. The cache header now includes the flag under YACP section version 49, documented in `file-formats.md`; old sections rebuild.

Pro `1.7.2-yacp.152` verified off/on and the Text Settings toggle. Disabled rendering is pixel-identical to `1.7.2-yacp.150` on the synthetic CSS fixture. First-line x positions changed from 10/9/10 to 35/34/60 for plain, zero-indent and two-em paragraphs. Hanging, centered, right-aligned and list rows retained their x positions. The preview and all 34 translations include the option. More EPUB diversity and real hardware remain required.

The section-48 to section-49 Pro fixture rebuilt a copied partial chapter and retained exactly spine 0, page 1, visible-text offset 668. The original section hash stayed unchanged. A second full rehash of all 5,747 reference files found zero differences in both the original YACP checkout and frozen snapshot; six Git worktrees remain registered.

## Reader-only dark mode

ActivityManager applies the optional reader polarity only to ReaderActivity instances. Child menus, dictionary and settings retain their normal colors unless CrossPoint global Night Mode is enabled. Text Settings exposes the separate switch. Quick Resume stores the displayed polarity in the existing state save and restores it before seeding the X3 controller baseline. No page-turn allocation or extra state write is added; the raw framebuffer format is unchanged.

Pro simulator `1.7.2-yacp.154` passed off, reader-only, global-night and UI-toggle scenarios. English screenshots distinguish the reader, settings and Home polarities. X3 `1.7.2-yacp.155` resumed the exact chapter and polarity in normal and dark scenarios, consumed the retained frame and preserved exactly one autonomy sleep sample. Before/after black-pixel fractions were equal (1.46% normal and 98.54% dark); hardware waveform correctness remains untested.

Standard `1.7.2-yacp.153` passed paragraph indents with 6,238,453 application-flash bytes and 58,088 static-RAM bytes, SHA-256 `788bf57a9bebfca6bb3bb6a41c978d2cd94cfd3baa927c40aeab09e28007fadf`. Standard `1.7.2-yacp.156` passed reader dark mode with 6,239,859 application-flash bytes and 58,088 static-RAM bytes, SHA-256 `0c8b0791bbd81711d0b27cf2d1a762aa8c40c54558fd7ccb05f9c47692b0d526`.

The settings candidate v2 now converts reader dark mode and paragraph indents. It retains 27 explicit blockers out of 76 real keys. All 45 Python migration/format tests pass. Candidate v1 and the original snapshot remain unchanged; v2 is still not an installable migration.

## File browser display choices

CrossPoint already owns the two-line list, row virtualization, UTF-8 display composition, scrolling and touch hit areas. The port adds one/two-line selection and optional extension hiding through the common settings list. A fresh install retains two lines; the source YACP value maps explicitly. The one-line mode inherits the standard single-line body style from the SDK, while the wrapped mode keeps CrossPoint's small text. No file-list allocation or filesystem-path transformation was added.

Pro `1.7.2-yacp.157` verifies long accented filenames, both row modes, hidden extensions and tapping the first book. Original test-book hashes stay unchanged. The default two-line shown-extension capture is pixel-identical to `1.7.2-yacp.154`. An initial automation tap above the first row was corrected into its actual hit area before the passing scenarios. All 34 languages include the four labels.

Standard `1.7.2-yacp.158` passed the browser changes with 6,242,477 application-flash bytes and 58,088 static-RAM bytes, SHA-256 `be0734903c04e8e994fbcfedcbe2cb71bc12c0c9c2a55433b7c06e0d259b3b17`. Candidate v3 adds the two browser choices, leaving 25 blockers. A private Pro 157 experiment retained all 53 candidate values after saving and resumed the Congo passage at spine 22, offset zero. Original and prior experiments remain preserved.

## Current indicator

The X3 USB-state poll already reads BQ27220 Current at 0x0C every second. Four atomic bytes now publish that measurement and its validity to the renderer. The optional status indicator consumes this cached value without another I2C call, SD write, allocation or worker. Failed polling invalidates the sample; zero remains a valid current. Other supported Xteink profiles do not show the setting.

The reported value is the documented signed Current measurement updated once per second, rather than a claim of averaged or sleep current. See TI's [BQ27220 technical reference, section 2.8](https://www.ti.com/lit/ug/sluubd4a/sluubd4a.pdf). Original YACP read 0x14 as AverageCurrent; that path is not carried forward. No hardware energy improvement is claimed.

X3 simulator `1.7.2-yacp.159` verifies button navigation, show/hide and persistence; its gauge is not simulated. Standard `1.7.2-yacp.160` builds the actual HAL path: 6,243,875 application-flash bytes and 58,088 static-RAM bytes, SHA-256 `9a885fe9b0c438c402cd977e09cb746fc66501b8f3c76874203b4b0d658275c1`. On a real X3, check signed mA on battery and USB, hide/show, narrow orientations and failed gauge reads.


## Network packet adaptation

The missing YACP station-output hook is retained behind HalNetwork. Installation runs on the TCP/IP thread through the installed SDK's tcpip_callback_wait API. TCP IPv4 headers get the Don't Fragment bit and a recomputed checksum; UDP, existing fragments, incomplete headers and already-marked packets remain unchanged. Explicit byte reads tolerate unaligned pbufs, with no per-packet allocation. CrossPoint already disables modem sleep during progress synchronization and resets networking on exit; its policy stays in use.

The host test passed under address/undefined-behavior sanitizers for header options, fragmentation, idempotence, other protocols and malformed lengths. Standard 1.7.2-yacp.162 passed with 6,244,529 application-flash bytes and 58,096 static-RAM bytes; binary SHA-256 8d14540cd0cbf9bdde6efa0c2d007bb8d471cf54da52bb934eb16cd8fa490991. This preserves the local YACP behavior, previously attributed to Doxxxxxx's network work. It is not evidence of a throughput or battery gain. Hardware must verify KOReader sync, ordinary downloads, reconnects and errors on the actual access point.

## Progress-bar position

The optional top bar reserves space above EPUB and TXT text, retaining CrossPoint's layout and visible-text anchors. Dictionary and footnote selectors use the same inset. XTC keeps its existing optional overlay, with the bar at the top of that overlay. The settings preview represents the same ordering. One byte stores the preference, with fixed settings-row storage and no per-page allocation or new cache field. Existing viewport dimensions invalidate affected caches.

Pro 1.7.2-yacp.163 passes the settings popup/persistence and portrait EPUB, TXT and XTC scenarios. The default bottom-bar EPUB screenshot is pixel-identical to Pro 161 on the same fixture. A landscape screenshot is clipped at x=480 in both 161 and 163; this predates the top-bar change. Full landscape validation remains open rather than being inferred from the passing portrait tests. All 34 languages include the position label.


Standard 1.7.2-yacp.164 passed the progress-position block with 6,246,395 application-flash bytes and 58,104 static-RAM bytes, binary SHA-256 06af0c80e2a5b8b8e0317e84a33e83493e9d19d6eea3fc98cec8b6d28014dde7. A copied partial chapter reflowed from 19 to 21 estimated pages while preserving exactly spine 0, page 1, visible-text offset 593. The source cache remained unchanged.

## Global-statistics backup

The manual action uses CrossPoint dialogs with Cancel selected initially. X3 also retains the optional, date-qualified backup after reader exit and before deep sleep. It stores the same global-statistics bytes and YACP names under /.crossink-stats-backup, retaining seven recognized archives. This is a backup of local totals, not of per-book statistics, daily-minute details, synced devices or the complete SD. The migration snapshot remains separate and complete.

Writing uses a temporary file, close and full byte comparison before promotion. A previous target survives failure between renames and is recovered on retry. The scan uses fixed name buffers, with no directory-size-dependent vector. Its bounded, maintenance-only stack uses 319 bytes in the entry function and at most 256 additional bytes in the write/verification helpers, apart from HAL frames. It runs outside page rendering and replaces the old growing vector. No latency or energy reduction is claimed. Archives with unrecognized names are not pruned; numeric backup counters sort numerically beyond 999.

Sanitized host tests cover versions 1/2/3, unknown/truncated formats, identical-content retries, write/close/rename failure, interrupted promotion, the seven-file bound, unrelated filenames and counter overflow. Eight actual copied files, the current global totals and seven archived backups, passed exact in-memory backup checks with unchanged source hashes. Pro 165 verifies Save, Cancel and newer-format refusal, preserving the source bytes in every case. Restore workflows and nearby synchronization remain pending. All five labels are supplied in every language. Settings candidate v5 adds the automatic-backup preference and still has 23 explicit blockers; it cannot be activated.


Standard 1.7.2-yacp.166 passed the backup block with 6,257,227 application-flash bytes and 58,104 static-RAM bytes, SHA-256 6ee78922aee657782aa0d88900fafc4236b8bffd1cdf66731e5a371fbeaa2696. X3 simulator 167 verifies the automatic date-qualified backup against the exact saved global-statistics bytes, followed by Quick Resume with one autonomy sample. Physical SD interruption and device timing remain untested.


## YACP OTA preparation

The CrossPoint update dialogs remain. Release selection uses YACP's independent numbering and exact filenames for C3 Standard/XLarge, Pro Standard/XLarge and Classic Standard/XLarge. A release without the requested variant yields no update. No fallthrough to CrossPoint or another board is allowed. Stable releases outrank development builds with the same public version, and failed checks clear earlier candidates.

Release metadata uses esp_http_client with the installed framework CA bundle, hostname validation, no redirects and no insecure fallback. This is separate from upstream's wolfSSL bulk transport, which currently disables certificate verification. A mandatory SHA-256 from that authenticated metadata and the published size must match the entire SD file. The embedded version and variant must also agree. CrossPoint's full image/chip/board validator then runs before any partition erase. TLS metadata compatibility and heap behavior on hardware still require validation; failures must remain errors, not insecure retries.

The streaming parser uses one fallible allocation (its fixed state exceeds 2 KiB), released before downloading. SD hashing uses a separate fallible 1 KiB buffer after the network session closes. These maintenance allocations add no reader-path work. The temporary path is /.crosspoint/yacp-ota-update.bin. Download failures and hash errors leave the active firmware unchanged. HTTP transfers now restore their previous Wi-Fi power-save mode, including nested use.

Sanitized host tests cover version overflow, all six filename variants, arbitrary JSON chunking and key order, truncated metadata, stale candidates, missing/wrong hashes, wrong embedded variant/version, undersized partitions, missing partitions, read/download failure and validator rejection. Installation is stubbed in memory for these tests. Hardware acceptance still requires real network/TLS, SD and image-validation scenarios; no device has been flashed. The remote installation API is verified separately below.

XLarge 1.7.2-yacp.168 passed the preceding backup/progress blocks: 6,176,001 application-flash bytes and 58,040 static-RAM bytes, binary SHA-256 20fc5a930f9d108a745844784e88210a3936247a3affcc0e9c7dadcadf7c4132.


Standard 1.7.2-yacp.169 passed staged OTA compilation: 6,428,343 application-flash bytes (98.1%) and 58,616 static-RAM bytes. Binary SHA-256: 43ba9a2d24647d018f036f3c5ef4d637870683ae6c0499e79f711fb337b00e2f. The remaining application-flash budget is 125,257 bytes; subsequent features must fit without silently changing the partition layout. Host test reports are reproducible with scripts/verify_yacp_ota_host.py. Actual YACP v1.7.1 release metadata parsed successfully and was rejected as older than this development image before download.


The remote POST /api/firmware/install route accepts the existing form or JSON path plus confirm=install. It refuses active uploads, relative/traversal/hidden paths, malformed JSON, missing files and directories. It closes the inspection handle before invoking CrossPoint's validator/flasher, returns an error on failure and restarts only on success. No new device UI labels are introduced; response strings are the API protocol. Pro simulator 170 passed 12 request scenarios, with both valid form and JSON requests returning 503 because installation is explicitly disabled in simulator builds. The test file remained byte-identical and the server remained available. No successful physical installation has been tested.


Private Pro 170 testing of settings candidate v5 retained all 55 converted values after reader exit and saving. Congo resumed at spine 22, page 0, visible-text offset zero. The test used another copy of the previous fixture, with English UI and reading-statistics tracking disabled for the experiment. The prior fixture, source candidate and original SD snapshot remained untouched. Its 23 unsupported settings still prevent activating this as a full migration.


Standard 1.7.2-yacp.171 also passed the Web API block: 6,430,467 application-flash bytes and 58,616 static-RAM bytes. Binary SHA-256: dda526925638b7363161de7e0363d66e441769cb74299f0e3d26de9da4e6c1b7. The real-data Pro 170 capture shows the same "Dans le cercueil" passage as the supplied photo. It is an initial simulator check, not a matched physical-display comparison.


## Per-book sleep galleries

The original YACP folder identity remains /.book-galleries/<FNV64>-<label>, with .last-sleep-bmp remembering the previous image. Custom, Cover and Cover/Custom sleep modes consult the current reader's gallery at the same points as YACP. Quick Resume retains priority. Missing galleries or invalid BMPs fall back to CrossPoint's ordinary cover/custom behavior. The selected image is drawn by the unchanged CrossPoint bitmap/grayscale pipeline; adaptive YACP tone mapping and cached grayscale planes remain separate pending work.

Selection uses a single 768-byte fallible workspace plus a 384-byte fallible path buffer, allocated only on a sleep transition. The workspace is freed before bitmap decoding. Reservoir sampling avoids a directory-size-dependent filename vector, and the last valid image is excluded when another valid image exists. Marker writes happen after rendering and closing the image file; a failed marker write is logged. No per-page work is added. Existing global wallpaper rotation remains CrossPoint's implementation.

Sanitized host tests verify stable folder identity, 100 non-repeating selections, independent book folders, single-image galleries, missing/invalid images and marker write/close failures. X3 simulator 173 verifies two consecutive sleeps choose different images, a second book uses its own image, and an absent gallery uses /sleep.bmp. All fixture BMP hashes remain unchanged. English screenshots were inspected. Build 172 failed on a simulator-only random-number API mismatch; 173 uses a simulator generator and the verified ESP esp_random.h API on hardware.


Standard 1.7.2-yacp.174 passed per-book galleries: 6,434,457 application-flash bytes and 58,616 static-RAM bytes. Binary SHA-256: f0230c3aa9f405c66685d334a0ec2fc58227af359d40bdcde6fd04fe3072a625. Adaptive tone mapping is being validated as filter value 3; upstream values 0/1/2 retain their existing meaning.


## Adaptive sleep-image tone

Filter values 0/1/2 remain CrossPoint's existing choices. New value 3 applies YACP's 1st/99th-percentile luminance range, minimum range 96 and 3/4 contrast blend before native four-level quantization. Highlights above 242 retain the original protection. All 34 translations include the additional choice. Existing YACP filter 0 maps explicitly to new value 3 in candidate v6; the private source remains unchanged and 22 settings still block activation.

Only an explicit adaptive sleep render scans the BMP. Two fallible buffers hold the 256-entry histogram (1 KiB) and one packed input row (up to 8 KiB under existing 2048-pixel/32-bpp limits); both are freed before the display passes. Each Bitmap gains three small state fields. Ordinary text rendering adds no work, and non-adaptive bitmap decoding performs no analysis or new allocation. The existing CrossPoint dithering and panel transport remain in place.

Actual decoder probes compiled from the preserved YACP source, the pre-change CrossPoint-based source and the new source match byte-for-byte across 11 fixtures: 1/2/4/8/24/32-bpp, top-down, single-pixel, flat, narrow-range and bright images. Native/legacy modes match CrossPoint; adaptive matches YACP. Address/undefined sanitizers pass, including repeated row decoding. X3 175 verifies the translated settings option, persistence and adaptive rendering; the off-mode sleep capture is pixel-identical to X3 173. The initial UI test navigated to the Reader tab by mistake; its corrected Display-tab sequence passes. Cached grayscale planes and real-panel acceptance remain pending.

The build wrapper copies only Bitmap.h and Bitmap.cpp into the simulator renderer directory. Its other compatibility patches stay in the WSL mirror. These two files were byte-identical before this port.


## Display power and date compatibility

YACP's displayPowerSaving member was stored under fadingFix (reference SettingsList.h:314). CrossPoint already passes fadingFix into HalDisplay's turnOffScreen argument for monochrome and grayscale, and disables asynchronous refresh while it is enabled (GfxRenderer.cpp:1740, 1756, 2306, 2336). FreeInkDisplay::deepSleep synchronizes pending work and delegates to the controller. The X3 UC8253, X4 UC8279 and SSD1677 drivers implement the requested power-off and sleep operations. The existing S3 settings visibility and controller waveforms remain CrossPoint's. Candidate v7 preserves the persisted boolean, with invalid values blocked. This is source-level equivalence, not a measured battery claim. Verify current consumption and the first text/image page after wake on each physical controller.

Standard 176 passed adaptive BMP compilation: 6,436,001 application bytes, 58,616 static-RAM bytes; firmware SHA-256 c934bb8c248f7230378ac96b9d807877f75eabb14d42922be652fcbb108395a5. Application headroom is 117,599 bytes.

YACP's independent clockDateHasBeenSynced flag is persisted directly, converted separately from clockHasBeenSynced and set only after a successful full NTP sync. The existing automatic sync also handles legacy time-only settings. The Home date is X3-only, uses CrossPoint's header right-label slot and localized existing month labels. CrossPoint localTime already applies the timezone; the date path adds no second UTC offset, heap allocation or periodic task. Autonomy keeps its original trust gate: an undated cycle is not retroactively assigned an invented start date when NTP becomes available. A later new charge cycle can be dated.

Host sanitizer tests pass missing/untrusted clocks, local year rollover, X3-only display, invalid date and short-buffer cases. Extended autonomy tests and the real state round-trip pass; an initial test incorrectly expected an existing undated cycle to gain a date and was corrected to require a new cycle. X3 simulator 177 passes three Home/settings-save cases (untrusted, date, date plus clock), with English screenshots reviewed. Physical RTC sync/failure and no-RTC-board behavior remain untested. Candidate v8 preserves all 76 source keys in its decision report and leaves 20 blockers. All 54 Python tests pass.


## C3 reading-only front buttons

CrossPoint's ButtonRemapActivity now accepts a reader-profile mode on X3 and the original X4. Completing its four assignments enables YACP's separate reader mapping; Up resets and disables it, and Down cancels without changes. Its duplicate detection, rendering and navigation remain the CrossPoint implementation. The profile follows the reader lifetime, including its child menus, and is cleared when leaving the reader. Hint labels use the same mapping as physical button events. Five scalar settings are persisted on every board, but the alternate input mapping and settings action are compiled only for C3. All 34 languages include the additional remap title. No new buffer, task or per-page write is introduced.

X3 simulator 179 passes assignment, duplicate rejection, previous/next page, reader-menu routing, cancel and reset. Default menu mappings stay unchanged. The final held reader Back uses CrossPoint's file-browser route; this is separate from YACP's still-pending configurable long-press actions. Pro 180 touch page turning, long Home toolbar and short Home return are pixel-identical with the imported C3 profile enabled and disabled. The initial Pro test incorrectly expected the C3 full-screen menu and exited from the book's final page, triggering the existing completion prompt. The corrected scenario verifies CrossPoint's default toolbar and returns from the first chapter. No firmware behavior was changed to satisfy the test.

Private settings candidate v9 preserves 63 converted values through Pro 180 reader exit. The Congo fixture resumes spine 22, page zero, visible-text offset zero, with 12 chapter pages under these settings. The source snapshot and previous experiments remain separate. Fifteen source settings still block activation; all 55 Python tests pass.

Standard 178 passed the date block: 6,436,519 application bytes, 58,616 static-RAM bytes; firmware SHA-256 b19c4827e2a40e33f13e8626e52669fb7641effe6e9abf52f9192838e431ebf3.


## Reader-memory comparison, first findings

CrossPoint 1.6.5 already stores layout word bytes in 2 KiB fallible chunks (WordStore.h:9), frees consumed chunks and asks MemoryManager to reclaim rebuildable caches before one retry (WordStore.cpp:17). Injected host allocation failures verify unchanged prior words/output handles, failed initial metadata allocation, successful single reclaim retry and shared suffix lifetime. The code is unchanged from the pinned upstream tag. This does not prove a bound for every paragraph metadata array or real C3 fragmentation.

The upstream parser soft-flushes large paragraphs (ChapterHtmlSlimParser.cpp:1788), caps navigation anchors at 1024 and fails a section when text allocation drops content (parseStep at line 2114). EpubReaderActivity gates background building on 32 KiB free heap and 16 KiB largest block, builds at most two pages per tick and warms the next page once after a 400 ms idle delay. Page-load retries are capped at three. YACP's alternate safe-layout profiles and silent chapter indexing therefore need a measured missing benefit before replacing any of these paths. Low-memory C3 hardware, full long-CJK-chapter completion and direct end jumps remain pending. No broad memory-performance claim is made.

CrossPoint's unchanged UTF-8 code passes all 847 Latin/Vietnamese composition pairs covered by the preserved YACP table, both as strings and in-place buffers. A probe compiled against the actual preserved YACP code passes the same pairs. CrossPoint additionally passes an algorithmic Hangul L+V+T case. ASCII and Chinese text remain unchanged. Keep CrossPoint's implementation. The existing adaptive-BMP host fixtures already include non-byte-aligned widths 1/9/11/13/17/19; the ordinary modes match the pre-port decoder. CJK UI font-size fallback remains under investigation with the copied SD family.

A missing SD font family now retains its selected name and point size just like a corrupt font file. The unloaded renderer uses the built-in fallback. Pro 182 verifies a missing Atkinson family, normal reading, a subsequent boot with its copied files restored and successful reloading, with the archived font hashes unchanged. This closes two upstream branches that previously cleared the saved preference.

Standard 181 passed the C3 reader-profile block: 6,439,039 application bytes and 58,632 static-RAM bytes; firmware SHA-256 f1749113f95a4c1a69a9e9092b679c1f74ddf33fe3440af914f0ff2f0cf3852d.

## Upstream reader equivalence and CJK metadata

The CrossPoint UTF-8 composition table, implementation and WordStore remain unchanged from the pinned tag, as recorded in `qa-artifacts/upstream-reading-source-183.json`. Host sanitizer tests cover 847 accent-composition pairs against both source trees, plus CrossPoint in-place normalization and algorithmic Hangul composition. WordStore fault tests exercise failed allocation, reclaim/retry, chunk lifetime, suffix references and preservation of earlier words. They do not establish a global bound for all paragraph metadata or substitute for real C3 heap measurements.

The Pro 182/184 CJK fixture contains one 10,800-codepoint paragraph and the copied NotoSansSC family, which supplies only 14 pt. CrossPoint reads it, but its exact-size UI fallback lookup leaves the Home title/author incomplete. The port now registers the YACP heading and reuses the nearest available raster size, at most 16 pt for UI safety. The manager reuses already loaded fonts: this fixture adds no font object or glyph cache. The fallback map gains one heading entry when the family is loaded, not per page. Latin strings continue through the built-in font. Vector families reuse the existing 12 pt UI instance for the heading, with no fourth FreeType face/cache. Large-print-only raster families above 16 pt still need a smaller UI font file.

`verify_yacp_cjk_reader.py` captures Home, two reader pages and return. Pro 184 title and author were visually reviewed. The two reader pages are pixel-identical to Pro 182. Evidence is in `qa-artifacts/cjk-pro-184-captured`. This verifies the first pages, not full-chapter completion, direct final-page jumps or real SD timing. Standard 183 compiled the missing-family preference fix (6,439,199 application bytes, 58,632 static RAM bytes; firmware SHA-256 `1edfcceb89e00c612b688bed309f29e69352f14b42f6dc597e00a8b45d75ae45`).

## Independent long presses

The side-button setting is available on every model with physical Up and Down keys, including X3, original X4, X4 Classic and X4 Pro. It retains the five YACP stored values: chapter (0), font size (1), off (2), orientation (3), previous page (4). The default, inherit (5), follows the existing CrossPoint front-button setting until the user chooses a separate mode. The front font-size action retains YACP value 3; existing CrossPoint 0/1/2 values keep their meanings. These four new labels are translated in all 34 locales.

The shortcut uses CrossPoint's existing one-shot hold detection and suppressed-release mechanism. Previous-page mode also works with both side buttons assigned to Next. EPUB font steps reuse CrossPoint's live text reflow and visible-offset preservation. Finding the next size scans existing entries and adds no temporary size-vector allocation. Built-in use does not discover the SD catalogue. TXT font/rotation reflow keeps its visible byte offset with one `size_t` field. XTC consumes unsupported font/rotation actions without moving its fixed-image page. As in current YACP, the side font-size action is specific to EPUB; front font-size also applies to TXT.

`verify_yacp_long_press.py` passed nine scenarios on X3 363, nine on original X4 375, eight on X4 Pro 353 and nine on X4 Classic 354. They cover two-second holds, unchanged release screenshots, chapter vs ordinary turn, front/side independence, the both-side Next layout and an invalid imported value. `verify_yacp_format_shortcuts.py` passed seven TXT/XTC cases, checking the old TXT byte offset against the rebuilt index rather than assuming identical page numbers. `verify_yacp_pro_button_isolation.py` on Pro 353 compared imported C3 front-button profiles on/off with the side mode held constant: Home toolbar, swipe and Home return have identical screenshots. These tests do not validate physical debounce or the separate orientation-aware mapping preferences, which remain pending.

The Pro 353 simulator also displayed the new Controls row and its six choices in English. Selecting Previous Page through touch saved `sideButtonLongPress = 4` on the isolated test card.

Firmware image checks passed on Standard 357, X4 Pro Standard 361 and X4 Classic Standard 371. Their embedded board tags and version strings match the intended targets. No device was flashed for this change.

For hardware acceptance, on X3, original X4, X4 Classic and X4 Pro open the same EPUB, advance two pages, choose side long press Previous Page and hold either active side button for two seconds. Exactly one backward turn should occur while held, and none on release. Repeat with both side buttons assigned to Next, then test chapter skip, one font-size step and rotation. On X4 Pro also confirm that Home, touch and frontlight retain their configured behavior. No flashing was performed for this change.

CJK metadata was also visually reviewed on X3 186. Standard 185 compiled this font fallback change (6,439,225 application bytes, 58,632 static RAM bytes; firmware SHA-256 `df40bbf1533c73ddbb348b4e69c09d95cbee9deeb855e8d84c93269024266d9b`).

## Reader orientation profiles

C3 front-button orientation retains Off, Navigation and All. Navigation swaps the reader's previous/next roles in both landscapes and inverted portrait. All additionally mirrors physical front-button positions in inverted portrait, including Back/Confirm. The side-button profile independently swaps its two positions in every non-portrait reader orientation. These explicit YACP profiles use the saved reading orientation in reader child menus, as before. New Default choices retain CrossPoint's live rendered-orientation policy. S3 retains but ignores both C3 profile values.

`MappedInputManager` routes the profiles before its navigation helpers and hint labels, avoiding a second direction swap. A one-byte physical held-button mask prevents a rotation from changing a held button into another command; it is cleared after release, along with the now-stale logical release state. It allocates nothing.

`verify_yacp_button_orientation.py` passed 24 X3 190 cases: front Off/Navigation/All, side Off/On, CrossPoint defaults, all three rotated orientations, a custom reversed front profile, and two-second holds that change the active mapping mid-press. Pro 191 profile on/off screenshots remain identical for side hold, Home toolbar, swipe and Home return. Current landscape screenshots still have the simulator clipping limitation documented above, so these checks establish routing and release handling, not final landscape visual quality.

The v11 conversion accounts for the real card's 76 keys with 12 explicit blockers. Its 66 output settings were saved and reloaded unchanged through Pro 191 on a private copy. The Congo fixture still resumes spine 22, page 0, text offset 0, with a complete 12-page rebuilt chapter. Original card and archive data were preserved. The 57 Python migration/data tests passed.

Standard 189 compiled the independent long-press block: 6,445,375 application bytes, 58,632 static RAM bytes; firmware SHA-256 `e59f4cc431daaf06921a306b668241f525dc4b309d68da5bc7167d91401fadad`.

## Book profile checkpoint

EPUB text settings now use a bounded 172-byte activity-owned profile store (compile-time maximum 192 bytes), with no separate heap allocation and no I/O on page turns. The CrossPoint settings serializer substitutes the saved global text defaults without mutating the live reader values. An explicit setting change saves the book profile through alternating verified slots. The files live under `/.crosspoint/book_profiles/`, outside the ordinary cache clearing path. Finished-book moves also move this directory when possible; a collision preserves the source.

Host tests exercise 62 truncated writes, 62 single-byte corruptions, future-version refusal, write/close failures and 1,000 unchanged saves without writes under address/undefined sanitizers. X3 203 verifies two-book isolation and reopening, and a book-specific inverted orientation plus dark mode across Quick Resume with pixel-identical before/after pages. The real v14 fixture keeps all 68 global settings unchanged, uses its 16 pt book profile and resumes spine 22 at offset zero. Its chapter cache finishes with ten pages. Automatic-turn interval persistence and end-to-end migration activation are still outstanding.

Tiny 204 disassembly measures a 336-byte `BookReaderSettings::save` frame and a 208-byte `readSlot` frame. This bounded scratch is used only when saving settings, outside page-turn loops, to encode and read back a complete record without adding a heap allocation. These figures exclude nested storage/JSON calls; physical stack high-water marks remain part of hardware verification.

The X3 203 finished-book scenario moves the EPUB to `/read` and retains the exact profile bytes at the new path identity. Source preservation was rechecked after this block: 5,747 files match in both the original YACP checkout and its frozen snapshot (`qa-artifacts/source-preservation-book-profile.json`). All six worktrees remain present.

## Custom automatic-turn interval checkpoint

CrossPoint cadence choices remain 1, 3, 6 and 12 pages per minute. The additional Custom choice opens its existing interval slider, from 5 to 120 seconds, on both the C3 menu and Pro toolbar. All 34 languages define the new title. A v2 profile distinguishes text and interval overrides; selecting only a cadence therefore does not freeze the global text defaults. Reopening a book keeps automatic turning inactive.

The actual profile codec passes 64 truncated-write and 64 byte-corruption cases, old text-only v1 upgrades, explicit unchanged interval selection, global-text inheritance for interval-only profiles, and write/close/future-version faults under sanitizers. All 64 Python migration/codec tests pass. Candidate v15 includes explicit profile flags and is repeatable; the captured seven profiles have no custom automatic interval. Nine global migration blockers remain.

X3 207 passes Save, Cancel and explicit unchanged selection. Pro 208 passes touch slider Save/Cancel, cadence activation and Home return; reopening keeps the saved interval without starting it. English captures were reviewed. Early X3 test fixtures were corrected to account for CrossPoint consuming the first Back to stop automatic turning and to avoid the unrelated last-page completion dialog; no firmware behavior was changed to bypass either.

Pro 205 compiled the preceding text-profile block at 6,531,088 bytes, leaving 22,512 bytes in the unchanged OTA slot. It predates the custom interval addition; the final S3 image still needs remeasurement.


## Boot and guide checkpoints (210 to 212)

C3 power-button startup now samples the physical ADC ladder twice, eight milliseconds apart, before settings can remap buttons. Priority is Back, then Up, then Down. Back selects a full panel initialization and Home; Up opens the existing recovery firmware picker; Down opens the existing Join Network flow directly. Held chord keys are consumed until release. CrossPoint already starts release-build serial logging asynchronously, so the former YACP USB delay workaround is not copied. The frozen temporary USB diagnostic behavior is not a product feature.

Tiny 210 compiled and Pro 211 passed the existing saved-network fallback scenario. Hardware checks remain required: power off from a reader, then test each physical chord on X3 with an inverted reading-button profile. Back must reach Home, Up must only open the firmware picker, and Down must reach network selection or the saved-network connection. Do not select a firmware or install it during this check. Repeat ordinary sleep/wake and USB-powered startup. Pro and Classic retain their upstream Down-key recovery route.

Guide Dots are now a decoration of CrossPoint's existing spaces. Tight gaps that cannot hold the glyph plus separation remain undecorated. This preserves CrossPoint line breaking rather than enlarging spaces. The Pro 212 comparison covers five pages with built-in fonts and four with the captured Atkinson SD font, including Focus Reading, hyphenation, mixed styles and a footnote link. Words, line coordinates, visible offsets and link geometry are identical with the mode off/on. The off image is pixel-identical to Pro 211; enabled images add ink only. The first comparison incorrectly included unused bytes after the NUL in cached link strings; the corrected comparator checks the actual target string and hitbox. All original test outputs are retained.

The Guide Dots control uses the upstream Style tab and preview. A Pro touch scenario enables it, leaves the book and reopens it: the book retains the switch while the global default stays off. Real C++ profile fault tests cover all 65 truncated writes and single-byte corruptions, legacy versions 1 and 2, guide scope, interval-only inheritance and failed write/close operations. A copied section-49 fixture resumes at visible offset 1404 after rebuilding as section 50.

Candidate v16 adds the guide setting to the verified conversion of the real Standard card, leaving eight global blockers. The original snapshot and its seven book profiles remain preserved. Activation is still disabled.


## Upstream comparison and flash checkpoint (215 to 217)

A separate managed, detached worktree `crosspoint-reference` contains the pristine pinned CrossPoint tag and SDK. Only ignored simulator configuration was added. Reference simulator artifact `CrossPoint-1.6.5-reference.1-simulator_x4_pro.elf` has SHA-256 `1274ec1de7b01f14288abd5dc022af2aa5bdbd7356033504b608e2b5519a8b22`. Its source checkout remains clean. This seventh worktree is an active comparison reference.

The privately copied Congo chapter containing the photographed passage was run on that reference and YACP Pro 212 with identical Atkinson Hyperlegible Next 16 pt settings and ordinary upstream line spacing. The whole reader image is pixel-identical; all eight generated pages have identical serialized text geometry. Exact settings from the earlier CrossInk photograph are unknown. The images and personal EPUB remain in the private SD archive, outside public documentation assets.

The Noto Sans 18 UI family is used by the SDK slider, whose TextStyle adapter selects only regular or bold. Removing the two unreferenced italic-style registrations leaves all four styles in the YACP reading families. Standard 215 compiled at 6,334,288 binary bytes, with 219,312 bytes left in the unchanged OTA slot. Compared with 213 this removes 140,544 binary bytes. Slider and value screenshots remain pixel-identical on X3 216 versus 207 and Pro 217 versus 208. S3 flash must still be remeasured.

Simulator 214 failed because the new physical C3 boot sampler was guarded by the wrong simulator macro. The two sites now use the existing SIMULATOR define; X3 216 and Pro 217 compiled. The physical ADC path remains hardware-only.

The additional CJK scenario starts in chapter two, crosses backward into the uncached final page of a 10,800-codepoint paragraph, leaves and reopens it. X3 216 reaches page 37 of 38 at offset 10655. Pro 217 and pristine CrossPoint both reach page 39 of 40 at offset 10606, with identical full-chapter geometry and final-page pixels. The final marker is present; reopening preserves exact progress and cache bytes. Simulator heap values are synthetic, so these results do not establish C3 memory safety under pressure.

CrossPoint already builds sections incrementally, paces background work to two emitted pages, limits the ahead window, lends its existing framebuffer for build scratch, gates work on free heap and largest block, and bounds page-load retries. Frozen YACP's silent next-chapter indexing can build an entire following chapter and switch rendering profiles after allocation failure. It is not copied without a demonstrated gain because the port must retain CrossPoint rendering. Hardware measurements of long CJK paragraphs, chapter transitions and low-memory recovery remain required.


## Sleep-plane cache checkpoint (220 and 221)

The custom-image and per-book gallery path now stores CrossPoint's BW/LSB/MSB frames in a separate disposable v2 cache. Cover and transparent-overlay rendering stays on its existing path. Controller, grayscale mode, orientation, dimensions, filter, fit/crop and fading setting form the key alongside a full source-content fingerprint. Corrupt or interrupted cache files are rejected before the first display pass. Source and cache handles never overlap.

The actual storage implementation passes 240 truncated files, 240 individual byte corruptions, 288 interrupted-write budgets, nine rendering-key changes, changed source contents and read/open/close failures under address and undefined-behavior sanitizers. The fake storage asserts that at most one handle is open. Pro 220 and X3 221 compare ordinary decoding, first cache write, cache hit, corrupt-cache fallback and same-size image replacement. The first three valid outputs are pixel-identical to the prior firmware, and original BMPs stay unchanged. The initial X3 capture happened between the BW and gray passes while host I/O was slow; the corrected run captures after completion. Original evidence is retained. Physical waveform quality, SD latency and energy use are not established by these checks.

Pro 223 and X3 224 repeat these scenarios after replacing small validation reads with borrowed-framebuffer scratch. Both pass, with unchanged pixels and original BMPs. The host harness also checks one source read for a source fitting the supplied scratch, and one header plus three whole-plane reads for validation. The fallback still uses a bounded 192-byte chunk when no scratch is supplied. The change adds no framebuffer or other heap buffer.

Standard 225 compiles the final cache path: 6,324,977 application bytes, 58,648 static RAM bytes, binary 6,339,056 bytes and 214,544 bytes left in the unchanged OTA slot. SHA-256: `b51bd36067df01502f17b5b00ba852b6d7e862591d6788f84451df18f763d990`.

## Named device checkpoint (226 and 227)

The existing CrossPoint keyboard edits the device label through a final System row. Names retain YACP's 2 to 20 UTF-8-byte limit. The fixed settings field uses 21 bytes; editing uses the existing fallible keyboard allocation only while that screen is open. The KOReader client borrows the effective label for its existing JSON payload. It does not change hardware identity, credentials or Wi-Fi hostname.

Pro 226 passes thirteen HTTP cases, including invalid requests rejected before any other setting is changed, maximum byte lengths, default reset, reopening and invalid-file fallback. X3 227 Save/Cancel and Pro 226 touch Save/Cancel pass. English screenshots were reviewed. A synthetic loopback KOReader server receives exactly one explicitly requested upload with the chosen label. No external sync account or personal progress was changed. All 34 locales contain the label. Hardware acceptance: edit the name on X3, restart, inspect System; repeat with Pro touch and check the label on a controlled sync peer.

Standard 228 compiles this block with 6,324,933 application bytes and 58,680 static RAM bytes. The verified binary is 6,339,024 bytes with 214,576 bytes left in the OTA slot, SHA-256 `cf365ffc40b8035a2ef0491d3a07a46daa67c3c0b69114d5d6a19e8bfccef2c5`.

## Recent books and refresh checkpoint (229 to 232)

Pro Standard 229 compiles the sleep cache and named-device block at 6,396,794 application bytes and 102,560 static RAM bytes. Its 6,401,808-byte binary leaves 151,792 bytes in the unchanged OTA partition. SHA-256: `3e2874e9b8850466629980180c4cadc6a2bd944dd2213438696723ffa035f7cd`. This remeasures the S3 margin after removal of unused Noto Sans 18 italic registrations; it predates the refresh and recent-capacity changes.

The real card contains twelve recent books, while upstream loading stops after ten. A baseline simulator scenario reproduced that loss on resave. Restoring the old YACP limit of eighteen retains CrossPoint's storage and navigation code. X3 230 opens the last of twelve and eighteen entries, moves it to the front and preserves every other entry and metadata field. The existing vector reserves only the loaded count, capped at eighteen. This adds at most eight records and their strings compared with upstream; it adds no framebuffer or per-turn allocation. Hardware heap measurements remain required.

Periodic X3 reinforcement uses the pinned SDK's differential grayscale-base operation on eligible text pages only. Counter zero explicitly requests cleanup after images, indexing or manual refresh; counter one denotes scheduled maintenance. Unsupported controllers, image pages, XTC and other devices keep upstream cleanup. The preference is retained but inactive on Pro/Classic. All thirty-four locales contain its setting labels. The 60-page choice is appended without changing CrossPoint's stored Never value of five.

Host traces under address/undefined sanitizers cover six cadences, interrupted cycles, every controller/content gate and 100,000 turns with Never. Eight X3 231 EPUB/TXT simulator scenarios with AA off/on verify periodic execution and explicit cleanup. Eight Pro 232 scenarios verify the stored preference never activates X3 reinforcement. Final pixels are identical with the preference off/on, and manual refresh preserves the displayed page. These checks do not establish physical flashing, ghosting or energy use. On both supported X3 controllers, check plain text, AA, an image followed by text, indexing and manual refresh at one-page and longer intervals. Repeat Pro Home, touch and frontlight acceptance on hardware.

## Standard 233 and preservation check

Standard 233 validates eighteen recents and periodic X3 reinforcement: application 6,328,353 bytes, static RAM 58,688 bytes, verified binary 6,342,432 bytes and OTA headroom 211,168 bytes. SHA-256: `1407b020b9a63ef4e4f667f8aa2d0a448a45700f96b56cae02b825feafc06a04`.

The 5,747 original YACP files and their frozen copies again match the preservation manifest exactly (`qa-artifacts/source-preservation-refresh.json`). All seven Git worktrees remain present. None was archived or deleted during this block.

## OPDS per-server choices (234 to 236)

An optional byte in each existing OPDS record preserves its download naming choice. Value 255 inherits CrossPoint's global setting; zero, one and two select author/title, title/author and title only. Existing CrossPoint records without the field inherit the global value. Migrated YACP records retain their explicit per-server choice, including mixed catalogues. Download construction still uses CrossPoint's filename sanitizer and downloader.

The server editor uses the existing OptionPopup with four existing translated labels, present in all 34 locales. Its fixed row array grows from five to six entries and the popup lives only for that activity. There is no per-page allocation or reading-path work. Failed store additions/updates now restore the previous in-memory record; moving its strings avoids a second copy of the old record. This does not claim power-loss atomicity of the underlying upstream JSON file save.

Actual C++ storage tests pass mixed overrides, global inheritance, serialization, failed-save rollback and aliasing under address/undefined sanitizers. Credential obfuscation is stubbed in this storage harness and tested separately by the pure conversion tests. Pro 234 passes eleven synthetic HTTP cases, rejects invalid values before mutation, retains an omitted field during edits and preserves choices on reload. X3 235 Save/Cancel passes and English screenshots were reviewed. The existing Web settings page exposes the same choices; its JavaScript tests cover five rendered states, four posted values and omitted unchanged passwords without network use. Pro 236 compiles the portal update. No external OPDS catalogue or real credential was used.

The actual Pro 236 download workflow passes all three per-server formats and global inheritance against a synthetic loopback catalogue. Each output has its expected filename and identical EPUB bytes, with one request per download. Standard 237 compiles this block: application 6,330,997 bytes, static RAM 58,688 bytes, binary 6,345,088 bytes and 208,512 bytes left in the unchanged OTA slot. SHA-256: `dd32dde9f0f949904cffe02be6cb30a22691bf3f25173baa4f6bc8460017f9ff`.

## Sleep-image Web preparation (238 to 242)

The portal uses the existing CrossPoint file-list, upload, download and deletion APIs. New prepare routes validate the selected book, close its handle and prepare its existing FNV64 gallery identity. General images use `/.sleep`. Upload requests remain sequential, and the existing server refuses filename collisions. The page chooses a numbered filename instead of replacing an existing image. Reader settings are not changed by upload.

The browser retains frozen YACP's crop, 35 to 100 percent crop size, three bounded grayscale thresholds, percentile stretch and Atkinson diffusion. Each selected image retains its adjustments. Only the current source is decoded; conversion buffers live in the browser, with no added firmware framebuffer or reading-path work. Output is a top-down uncompressed two-bit BMP. Five host cases, from a single pixel through both panel sizes and nonaligned widths, decode to the expected packed pixels through actual CrossPoint C++ under address/undefined sanitizers.

X3 241 and Pro 242 pass thirteen invalid-path cases, repeatable gallery creation and two gzip/ETag checks on isolated synthetic cards. Real Chrome automation covers two selected images, drag and button recropping, thresholds, reset, upload collisions, byte-derived preview equivalence, per-book selection and cancelled deletion. Desktop and mobile light/dark screenshots are English and were reviewed. No existing personal file was used or deleted.

The initial browser check found different resampling pixels after a canvas backend switch; requesting a frequent-read canvas makes reset reproducible. X3 240 caught a stale board-profile geometry in the simulator; status now uses initialized HAL display geometry, verified as 528 by 792 on X3 and 480 by 800 on Pro. The failed fixtures remain available alongside the corrected checks.

Physical acceptance: open File Transfer on X3, prepare a general image and a gallery image for a test EPUB, then select a custom sleep mode and verify sleep, wake and image cycling. Repeat on Pro, checking touch navigation and Home after leaving File Transfer. Confirm that a second upload with the same input name preserves the first image. Hardware SD behavior and display waveforms remain unverified.

## Statistics restoration scope and evidence

Standard 243 compiles the sleep-image portal: application 6,340,817 bytes, static RAM 58,688 bytes, verified binary 6,354,896 bytes and 198,704 bytes available in the unchanged OTA slot. SHA-256: `3283fc3edba57d1660f71de011d82ab8175d2d870d718955b91ccd923a30f58c`.

The frozen YACP sources contain the manual backup activity and automatic recovery from `global_stats.bin.bak`; no dedicated restore screen or restore Web endpoint was found. The port retains those observable behaviors. Restoring a chosen archive means placing a preserved copy at the canonical statistics path while the firmware is stopped. It restores global counters, time buckets and reading-day bits only; these archives do not contain per-book progress or the separate daily-minute history.

`verify_yacp_global_recovery.py` compiles the actual global-statistics loader/saver with address/undefined sanitizers and in-memory storage. It verifies all serialized fields for versions 1, 2 and 3, plus seven read-only real card archives, through direct restoration, missing primary, corrupt primary and truncated primary. Newer-format primary or backup files block saves instead of being replaced. All sixty cases pass, a second save retains every field, recovery reads perform no writes, and original archive digests are unchanged. This closes the restoration software scenario without inventing an additional YACP interface. Physical power-loss recovery and nearby-device exchange remain separate acceptance items.

## Landscape simulator diagnosis

The earlier 480-pixel crop reproduces with Pro 242 and SDL's accelerated offscreen renderer. Running the same binary and fixture with `SDL_RENDER_DRIVER=software` displays the entire 800 by 480 canvas. The native firmware, SDK and simulator sources were not changed to obtain this result. Use the software renderer for future headless orientation acceptance.

Pro 242 EPUB screenshots in both landscape directions, Pro 242 TXT landscape and X3 241 EPUB landscape show the full page, separate top progress bar and complete footer. Saved positions advance by one page. Counterclockwise landscape needs the opposite physical navigation button under the existing CrossPoint mapping; the initial test used the wrong button. Fixed-layout XTC deliberately initializes portrait regardless of the text-reader setting. Its verified scenario keeps that orientation and places the progress bar within CrossPoint's selected XTC overlay area. It does not claim landscape XTC support. Current-draw telemetry is not supplied by the simulator; real X3 gauge readings and device acceptance remain pending.

## YACP identity checkpoint (244 to 248)

Boot and default sleep use the generated 1,800-byte YACP bitmap, identical to the frozen source. The portal serves the canonical SVG with gzip and ETag. All five pages identify YACP and retain CrossPoint attribution. A 375-pixel Chrome check caught navigation overflow after adding the sleep-image link; wrapping the links resolves it on all five pages. The canonical SVG response matches the public asset byte for byte and a conditional request returns 304.

X3 245 and Pro 247 default sleep screenshots were reviewed. The first Pro scenario pressed Power during the startup sleep guard; the corrected scenario waits until after it expires, without changing firmware. The early initial capture is not counted as boot-screen verification. Physical boot and sleep remain pending. Standard 248 succeeds with 6,340,021 application bytes, 58,688 static RAM bytes, a 6,354,112-byte binary and 199,488 bytes of OTA headroom. SHA-256: `082079a7fe39e748c39ac0cf595982c4a3b041d844b5644101b7c49b13bdd9bb`.

Build 244 failed because the PlatformIO Python environment lacks Pillow. The existing ordinary Python can prepare the generated assets, and a content-hash cache then lets PlatformIO consume those exact outputs. Logs now use UTF-8 so compiler diagnostics containing translated strings remain readable. This does not remove the documented Pillow requirement for a fresh logo generation.

## Nearby statistics software checkpoint (249 and 250)

The File Transfer menu adds the legacy YACP statistics exchange through CrossPoint's list and touch components. The SDK's raw ESP-NOW transport is wrapped by `HalNearby`; its different CIFT file-transfer codec is not used. The activity allocates approximately 5.5 KB once for the SDK queue and dequeue buffer, then releases it on exit. Session and packet storage is bounded, and nothing is added to the ordinary reading path. Device identity remains the factory MAC; the editable name is a display label.

The CISS v1 codec emits the exact legacy bytes for statistics versions 1, 2 and 3. Host datagrams cover lost and duplicated packets, passive exchange, peer locking, unbound acknowledgements, timeout across timer wrap, unsupported versions, unavailable local statistics and failed saves. Snapshot tests cover 159 short-write budgets, failed promotion renames, recovery and identical-snapshot no-op saves under address/undefined sanitizers.

The aggregation regression reproduced double counting of valid temporary/manual copies. Canonical device filenames now identify readers. A complete `.previous` snapshot is used only if its matching canonical file is absent or invalid; newer formats are preserved and skipped. Repeated exchange replaces the same snapshot and cannot add another copy to totals. No source file is deleted during aggregation.

Pro 249 and X3 250 show the new entry and an honest simulator-unavailable message. Pro touch Back and X3 physical Back return Home, leave the synthetic local statistics unchanged and create no peer files. All nine labels are present in all 34 locales. These UI tests do not simulate a successful radio exchange.

Physical acceptance requires two readers: open File Transfer / Nearby Stats Sync on both, press Sync on only one, verify names and totals, repeat without increasing totals, then read on one device and exchange again. Repeat with one old YACP reader and one ported reader, check version mismatch and cancellation, interrupt a write on a disposable SD copy, and verify return Home followed by ordinary reading. No radio, SD durability or energy result is claimed from the host tests.

The final aggregation screenshots explicitly navigate to All Synced Devices on X3 250 and Pro 249. One local hour plus two remote hours displays three hours, with identical pixels after adding five temporary/backup/manual copies. The first X3 scripts stopped on earlier statistics pages because this simulator profile exposes a clock; the corrected scenario uses three Next actions. The seven real archives also pass all sixty recovery cases after the aggregation change, with unchanged source digests.

Tiny 251 compiled successfully with nearby statistics: application 6,370,541 bytes, static RAM 58,776 bytes, final image 6,384,624 bytes and OTA headroom 168,976 bytes. SHA-256: `58ef3684579e54601ebbe0a6d5cac884ea85b811b0cf202e7115316eaf0355ef`. This confirms compilation, not a physical radio exchange.

## Built-in Standard font acceptance

`verify_yacp_builtin_fonts.py` runs eight isolated English Pro 249 cards. Each family/size produces a distinct rendered image; actual serialized text contains all four style bits and the accented fixture words. The second-page progress is saved in all eight cases. The contact sheet was reviewed for visible regular, bold, italic and bold italic faces, accents and clipping. Large justified text can still have wide spaces according to CrossPoint layout; this test does not replace the same-book comparison against pristine CrossPoint. XLarge and physical e-ink rendering remain separate acceptance checks.

The same eight Standard scenarios pass on X3 250. Its contact sheet was also reviewed. The two boards use different viewport dimensions, so page counts are allowed to differ; each saved record nevertheless resumes the second page of its own generated section.

## Remaining controls and recent-book integration

The frozen `RecentBooksActivity.cpp:182` builds a contextual book menu, not only a list/grid preference. Preserve its actual reachable actions when adapting recents. Enum cases that do nothing in that activity do not establish an existing feature by themselves. CrossPoint's `HomeActivity.cpp:122` already has a fallible, one-parser-at-a-time thumbnail loader, and `CoverGridHomeUi` uses its native cover-grid component. Those are the reference mechanisms for the pending grid. The ordinary single-book YACP Home must continue to avoid cover generation.

The frozen `EpubReaderActivity.cpp:3298` dispatches the C3 reader commands. The target `ReaderActivity.cpp` handles Back centrally, and its EPUB activity currently retains CrossPoint's Confirm and Home actions. Long-Power handling must respect `main.cpp:741` wake-release tracking, the screenshot chord and the earlier Pro double-click handling. Missing commands must remain explicit migration blockers until their actual behavior is implemented; do not resolve them by silently resetting the user's preference.

## Classic Standard 252 checkpoint

The X4 Classic Standard build succeeds with the nearby-statistics and portal changes: application 6,417,831 bytes, static RAM 102,512 bytes, final image 6,422,848 bytes, OTA headroom 130,752 bytes. SHA-256: `687b365e10f424cda130f7c7a5e29cdaa0fd3ccb7734c7e8302c371a60b36361`. ESP image integrity, S3 chip identity, Classic board identity and the embedded Standard variant all pass. Both OTA slots retain their upstream sizes. This is a compile and file-format check, not a Classic hardware result.

The simulator now has explicit `simulator_x3_xlarge` and `simulator_x4_pro_xlarge` environments. They inherit the matching board's upstream input/display profile and add only `YACP_XLARGE`. The versioning wrapper accepts both, so XLarge captures use the actual conditional font registration and size catalogue.

## XLarge font acceptance (253 and 254)

Both XLarge simulator profiles compile. Six isolated cases on Pro 253 and six on X3 254 cover Lexend Deca and Bitter at 16, 18 and 20 pt, each with regular, bold, italic and bold italic text. The accented words survive the actual serialized page format, the second page is different, and its progress is persisted correctly. Both contact sheets were visually reviewed. At 16 pt, both captured pages and the saved progress match Standard exactly on each board.

Together with Standard, this is 28 simulator scenarios. It is fixture coverage, not exhaustive coverage of every supported glyph or physical e-ink behavior. The test runner is `scripts/verify_yacp_builtin_fonts.py`; use `--variant xlarge --standard-output <matching-board-standard-results>` for the common-size comparison.

Hardware acceptance: on X3 first, then Pro, open the same font specimen in Standard and XLarge, select both families and all offered sizes, check accents and all four faces, turn a page, sleep/wake and reopen at the same text. Compare the shared 16 pt size across variants. Use disposable test-card copies after installation is separately authorized. No physical font result or installation is claimed here.

## Recent-book views checkpoint (257 and 258)

Tiny 259 compiled successfully: application 6,379,315 bytes, static RAM 58,776 bytes, image 6,393,392 bytes and OTA headroom 160,208 bytes. SHA-256: `870764c4ed93fc4f29bae7a4a9d5beb81545c902af35e037145b54c6c96e2ba6`. Hardware acceptance remains pending.

YACP Home keeps its single-book display. Its secondary menu opens a dedicated Recent Books screen, selected according to Display / Recent Books: List or Grid. The list uses CrossPoint's list activity; the nine-cover grid uses its native cover-grid component. Both retain up to eighteen entries and start on the book selected at Home. Other Home menu actions keep their fixed positions regardless of the number of recents.

The menu retains only the selected book's metadata; the recent screen owns its bounded eighteen-record snapshot. The grid loads covers only for its current nine-book page, with one fallibly allocated EPUB/XTC parser at a time. Thumbnail generation and drawing use the existing CrossPoint paths. The cover cache has two additional small slot records and stores image regions only in PSRAM; it never falls back to a C3 framebuffer allocation. Button navigation has fixed hold state and no per-tick vector/callback allocations. Only the selected book's saved progress is read when selection changes.

Eight scenarios pass on each of X3 257 and Pro 258: list/grid access to entry 18, a missing file, selection retained from Home in both modes, both empty views, and ordinary Home with no generated cover/EPUB caches. The missing-file test initially expected eighteen saved entries after opening a book; CrossPoint's existing `RecentBooksStore::addBook()` prunes the absent entry at that point. The corrected expectation checks seventeen remaining entries and preserves their metadata and order. Grid and list screenshots were reviewed. Pro additionally passes real scripted touch entry, a swipe to page two and opening book 18 by touch. X3 Settings toggles List to Grid and back, with persisted values and reviewed English captures.

`RecentGridNavigationTest.cpp` covers all counts from zero through eighteen, horizontal reachability and partial last rows under address/undefined sanitizers. All 34 locales provide List/Grid labels. This checkpoint does not claim complete recent-book parity or hardware validation.

## Initial recent-book actions (260 and 261)

Tiny 262 succeeds: application 6,387,569 bytes, static RAM 58,776 bytes, image 6,401,648 bytes, OTA headroom 151,952 bytes. SHA-256: `2a3b866ec2643b9ea940b682fce83bb4764c94b4a700b363645725aba4bbda66`. This checkpoint precedes selective cache invalidation and the Home-menu successor correction.

Both views now open the same CrossPoint list-based book menu on a one-second Confirm hold or native touch long press. Open, Statistics, Reset Book Reader Settings and Remove from Recents are available where applicable. A hold consumes its release. Confirmation defaults to Cancel. Failed operations stay in the menu with an error header. The selected book and Home snapshot reload after a confirmed change, including removal of the currently featured book.

Reset writes a zero-override record to the alternate profile slot and verifies it before treating the operation as successful. It does not erase either slot, modify global preferences or freeze a copy of the current defaults. Subsequent global changes still apply. Unsupported profiles are preserved and block the operation. Host tests cover all 65 truncated writes, repeat resets without writes, inheritance of later global defaults and unsupported-format rejection. Existing profile isolation, corruption and write/close failure scenarios also pass with address/undefined sanitizers.

Eight simulator scenarios pass on X3 260 and ten on Pro 261: reset, cancellation, removal and statistics from both views, plus Pro touch holds on each view. They check the actual profile bytes, saved recent order, source EPUB digests, returned Home selection and reopened font size. Menu, confirmation and Home captures were reviewed in English. Statistics are read only when explicitly requested; no reader or metadata parser is allocated merely to create the four-row menu. All 34 locales provide the reset label.

Recent removal now rolls back the in-memory entry if persistence reports failure. An in-memory host test checks all eighteen removal positions, preserving title, author, cover and order after a failed save. This does not claim transactional SD writes for CrossPoint's general JSON store. The first host compile selected an unrelated test stub via include order; the corrected test explicitly includes the production store header.

Pending actions remain explicit: safe cache clearing, book/statistics deletion and the completion command with its move-to-Read behavior. The frozen CrossInk render-mode selector is excluded because CrossPoint remains the rendering reference. Annotation migration and hardware tests remain separate open items. On hardware, repeat long Confirm on X3 first, then touch hold on Pro; cancel reset, confirm reset, reopen at global font settings, and remove a recent entry while checking that its EPUB and progress remain on the SD card.

The actual C++ profile loader/resetter also accepts the one fully converted real-card profile in candidate v22, preserving its original bytes and inheriting subsequent global defaults after reset. Six other profiles still contain a blocked `publisherPageNumbers` field and were not emitted as complete runtime profiles by this check. The candidate JSON digests remain unchanged. This is not activation of the SD migration.

The recent-grid progress capture on fresh copies of the reset fixtures shows 50% on both X3 260 and Pro 261, matching the displayed first chapter of the two-chapter specimen. Progress and recent metadata remain byte-identical, and the source fixtures are unchanged. Both English screenshots were reviewed.

Two further Pro scenarios reject an unsupported profile reset, retain the original slot and show the error header in each view. The initial concurrent run lost two short scripted navigation events after capture while the firmware compiler was running; it accidentally exercised reader opening instead. The corrected, serialized scenarios leave more time after captures and use 250-ms navigation pulses. Their saved input scripts and logs confirm entry into Confirmation, rejection by the profile guard and return without opening a reader. The error capture was reviewed. No firmware change was used to obtain this result.

## Cache-action audit before the next block (historical, before 263)

The target `src/util/BookCacheUtils.cpp:23` still delegates to the upstream whole-directory removal. Its callers include downloads, Web replacement and file deletion. `src/activities/settings/ClearCacheActivity.cpp:96` also removes complete book directories. `src/activities/reader/EpubReaderActivity.cpp:1163` clears the EPUB cache and rewrites a page-based position afterwards. These paths must preserve YACP statistics and exact progress before the port can be accepted. Copying the old YACP move-out/move-back implementation directly would introduce a window in which an interruption strands the saved user state.

Review generated cache entries against the actual CrossPoint writers, then prefer a bounded selective invalidation that never moves progress, statistics or unknown files. The root metadata writers use `book.bin`, `spine.bin.tmp`, `toc.bin.tmp`, `.items.bin` and `css_rules.cache` with its `.tmp`/`.bak` companions. HTML/section/image and cover paths need the same exact-source audit. Explicit book deletion and replacement must retain their distinct semantics. No cache-delete acceptance or data-preservation claim is made for the current upstream paths.

Also align the secondary Home menu's selected statistics book with Home's successor after removal: the current menu falls back to the first recent, while Home retains the previous ordinal. The normal Home return/open scenarios pass, but this intermediate menu selection still needs correction.

## Selective cache invalidation and Home successor (263 to 268)

The preceding audit is now implemented. `BookCachePolicy.h` lists generated entries from the actual EPUB, CSS, image, section, HTML and TXT writers. `BookCacheUtils.cpp` invalidates the root index first, then visits only the known fixed subdirectories and generated files. Progress, statistics, reader profiles, bookmarks, clippings, unsupported future records and unknown files remain in place. No user state is temporarily moved. The helper stops at the first failed open or removal. Its policy is not a claim that every later upstream cache rebuild preserves arbitrary files placed inside generated subdirectories.

The invalidator uses one 96-byte entry-name buffer and a reused bounded-depth path string. It does not allocate an EPUB/XTC reader, recursively collect paths or add work to page turns. The manual reader command saves the exact position before releasing its section; a failed save prevents invalidation. Global maintenance and existing replacement/download callers share the selective helper. Explicit book deletion remains a separate unfinished workflow.

An in-memory host SD checks nineteen generated removals, twenty-four preserved entries, all nineteen injected removal failures followed by retry, index-first ordering, directory-open failure, repeat cleanup, invalid paths and EPUB/XTC/TXT/Markdown support. Address and undefined-behavior sanitizers pass (`book-cache-host-263-c`). This test deletes no host files.

The recent-book menu now has a confirmed Clear Cache action with Cancel selected initially. Fourteen simulator scenarios pass on X3 263, including cancellation from list and grid. Four targeted Pro 264 cases cover cache cancellation and the secondary Home statistics after removing the selected book. Both Home and its menu now retain the successor at the previous ordinal rather than disagreeing about the selected book. Reviewed captures show Action book 3 after removing Action book 2.

All 34 locales describe retained progress and statistics. A new cancellation scenario on X3 265 found that Back dismissed the confirmation but left the activity open. Version 266 fixes all non-confirming dismissals through one inactive-popup path, and removes the old unconfirmed button bypass. X3 266 passes Back and selected Cancel; Pro 267 passes a tap outside the dialog. Each retains every fixture byte and returns to Settings without invoking cleanup. English warning and returned-screen captures were reviewed. Tiny 268 succeeds: application 6,389,413 bytes, static RAM 58,776 bytes, image 6,403,488 bytes and OTA headroom 150,112 bytes. SHA-256: `d63a7407e2f1f873289b4b52cab02eafea2626395a2340aab40b7e59aa1aa06b`. Hardware acceptance remains pending.

On X3, first cancel global maintenance and the per-book action, then clear a copied book cache and reopen at the saved text with unchanged statistics. Repeat on Pro using touch cancellation and confirmation. Test interrupted cleanup only on a disposable SD copy. No physical card, reader firmware or published source was modified for these tests.

## Real Wi-Fi conversion with the supplied factory identity (2026-10-02)

The user supplied the source reader's factory MAC. Both saved networks in the verified Standard snapshot pass the CPV1 envelope and keyed integrity checks with that exact identity. No alternative key was tried. This resolves the missing source-identity blocker for Wi-Fi.

`prepare_yacp_wifi_candidate.py` creates a private, separate `wifi-candidate-v1` in the existing local SD archive. It retains the last-connected network and both credentials, converts to CrossPoint's obfuscated payload plus length/CRC fields and checks a complete round trip before writing. Its target is explicitly the same physical reader. A different reader, including the Pro, needs its own factory identity for a separate candidate. The origin records both identities only inside the private archive; generated reports contain counts and validation results rather than network names or passwords.

Repeated conversion produces identical bytes and rechecks the snapshot manifest. Five candidate tests cover interrupted outputs, wrong identities, changed snapshots, destination conflicts, path separation and MAC notation. The seven credential-format tests also pass. `verify_yacp_wifi_candidate.py` reads the real candidate with production `ObfuscationUtils.cpp` and `CredentialIntegrity.h`, using the host mbedcrypto implementation and a supplied factory-MAC adapter. Both entries decode, validate and re-encode identically under address/undefined sanitizers. This tests the credential codec, not the SD store lifecycle or a radio connection. All source and candidate bytes remain unchanged by verification.

No plaintext password is persisted or printed. The candidate is deliberately named `candidate-wifi.json`, has `ready_to_activate: false` and was not copied to a reader. Migration activation and connection to the original access points remain pending.

## Legacy bookmarks and unfinished chapters (269 and 270)

Seven pure conversion tests cover versions 2 to 5, both reviewed section-cache layouts, paragraph precedence, float page lookup, unique Unicode text anchors, missing/duplicate text, inflated-byte progress and filename collisions. The complete legacy document remains in the decision record. Conversion refuses partial book output when any entry lacks an unambiguous content anchor. This is preparation for migration, not a first-boot importer.

The initial X3 266 run passed all four 12-point formats, but the 16-point case exposed a runtime bug: `EpubReaderActivity.cpp` replaced an unresolved same-chapter bookmark with page zero while its section was still being built. The fix uses the existing pending-offset build path when the bookmark is beyond available pages. The partial-cache shortcut also tests the requested text offset before deciding that enough of the chapter exists. No new allocation, layout rule, menu action or ordinary page-turn work is introduced. A section rebuild is requested only for an explicit jump that cannot yet be resolved.

Five navigation cases now pass on X3 269 and five on Pro 270, using CrossPoint's list menu and bookmark screen. All four legacy formats land on the page containing source offset 3,536 at 12 points; the 16-point case lands on a later page containing that same offset. Tests inspect the actual section offset table and saved 10-byte progress, not just activity logs. Source EPUBs, binary bookmark fixtures and candidate JSON remain byte-identical. English bookmark and destination captures were reviewed on both devices. The Pro scenarios use its physical-button route to the supported list menu; touch bookmark acceptance is still separate.

The actual Standard SD snapshot has no binary bookmarks. No real bookmark conversion was therefore invented. Timestamps and older metadata remain in the migration review record, while CrossPoint's current UI displays its own summary and progress fields. Clippings, global annotation access, large-list C3 memory acceptance and transactional activation remain open. On hardware, import only a disposable copy, open the same bookmark before and after changing font size, then reopen the book and check the saved passage. Tiny 271 succeeds: application 6,389,421 bytes, static RAM 58,776 bytes, image 6,403,504 bytes, OTA headroom 150,096 bytes. SHA-256: `7a6fe94b9fb0b9e5caeef1422165b936accdcff800f85b8ca7b249e094cf721f`. All 98 Python checks pass. No hardware validation or activation is claimed.

## Numbering audit for the following block

Frozen `lib/Epub/Epub.cpp:1155` loads `META-INF/x-locations.json` (format `x-locations` or `crossink-locations`, version 1). `resolveReferencePage():1454` derives stable numbering from the chapter's word or character ranges and reading fraction. Character units take precedence when their totals and units-per-page are present; the legacy fallback is 1,500 units per reference page. This feature is distinct from CrossPoint's ordinary rendered-page count.

A read-only audit of the eighteen copied EPUB files finds one such manifest, 21,589 bytes. Its identity and manifest digest are retained privately in `sd-standard/reference-pagination-audit.json`. Thus the missing stable-numbering behavior cannot be dismissed as an unused preference. Porting should preserve those units without importing the old loader's whole-manifest temporary allocation onto the ordinary reading path.

Publisher markers are a separate behavior. Frozen `EpubReaderActivity.cpp:655` draws marker labels at the corresponding text line; `:721` reserves a minimum 15-pixel left margin when enabled. Its `Page.h` limits each page to eight labels. Retaining this feature requires mapping publisher anchors into CrossPoint pages and a documented cache-format update, not replacing its justification or line-breaking engine. Both numbering settings and six affected real book profiles remain blocked until the relevant runtime behavior and scenarios pass.

## Stable reference counter (272 and 273)

The optional stable counter is now ported. `ReferencePageParser` retains only the selected chapter's range and document totals. A fixed grammar guard rejects incomplete documents, malformed punctuation, oversized tokens and trailing content. The parser is allocated fallibly at chapter entry only when enabled and released before page rendering. The reader retains twenty bytes of counter data and small lookup flags, rather than a complete manifest or chapter array. No manifest lookup runs with the option disabled; no parsing occurs on ordinary page turns within the chapter.

The host suite passes 112 cases under address and undefined-behavior sanitizers, including all 72 chapter entries in the one real copied manifest. The copied EPUB remains unchanged. Four scenarios each on X3 272 and Pro 273 cover enabled, disabled, missing and malformed manifests. Serialized sections and saved progress are byte-identical, and image differences are confined to the status counter. Missing and malformed manifests render identical to the ordinary counter. English captures were reviewed, including the second chapter's 11/20 counter. The algorithm retains the old chapter-fraction convention; font-independent exact character anchoring is not claimed.

The option is translated in all 34 locales. Candidate v23 maps `stablePageNumbers`, reducing real global blockers from five to four, with all seven book profiles and twelve recents preserved. Publisher numbering remains distinct and unresolved at this checkpoint. Hardware verification should enable the option on the copied manifest-bearing EPUB, change chapters, reopen, then try an EPUB without a manifest. Check the same passage and ordinary progress before and after toggling it. No source card or firmware was changed.

## Publisher annotations and complete book-profile conversion (275 to 278)

Publisher pagebreak labels now use CrossPoint visible-text offsets without flushing or splitting words. The optional source-range metadata costs four bytes per TextBlock and does not change its serialized text. Enabled chapters allocate a fallible, bounded 1,280-byte look-ahead only after encountering a marker; pages allocate 144 bytes only when they contain labels. The look-ahead caps at 64 pending annotations and logs overflow. The original limit of eight labels per page is retained. Markers inside tables remain excluded, as in frozen YACP. Missing labels do not affect reading.

The margin keeps the original minimum of 15 pixels when enabled. Labels use CrossPoint's small UI font; closely spaced labels no longer overlap vertically. A label that cannot fit below previous labels is omitted rather than painting over the footer. The same helper participates in prewarming, monochrome, image-placeholder and grayscale passes. No label allocation or source-range calculation occurs in ordinary reading with the option off. The empty pointer/count and four-byte line metadata remain resident.

Both X3 277 and Pro 278 preserve serialized line geometry, visible-text offsets and saved progress when toggled at a fixed 20-pixel margin. Pixel changes are confined to the left margin. The specimen includes a marker inside a word, an aria-label fallback and Roman numerals. Eighty input markers exercise the bounded path on each simulator, producing eight valid page records. English captures were reviewed. Pro text controls enable the feature only for the current book, preserve the global setting and restore it after reopening.

Section 51 and partial sentinel 231 invalidate obsolete generated pages. An actual format-50 simulator cache rebuilt as 51 retains source offset 3,223 on the same page. All twelve page offsets and text geometries match; the separate saved chapter-page estimate changes during the incremental rebuild and is not used as the migration anchor. The prior fixture remains unchanged.

Book profile v4 appends the publisher toggle. Host checks cover all 66 truncated-write positions, 66 corrupt bytes, versions 1 to 3, alternate-slot recovery, reset interruption, future-version guards and both guide/publisher inheritance. Candidate v24 converts all seven real profiles; the C++ loader/resetter accepts each under address and undefined-behavior sanitizers and retains original bytes. It leaves three global blockers, all related to legacy button holds. Complete migration activation and hardware acceptance are still pending.

On hardware, start on X3 with a copied EPUB containing publisher pagebreaks, toggle the book setting, reopen, then repeat on Pro using touch. Check margin labels, ordinary word spacing, retained passage after cache rebuild, footnote/dictionary hit positions and grayscale refresh.


## Publisher Tiny build 279

Tiny 279 succeeds: application 6,398,063 bytes, static RAM 58,776 bytes, image 6,412,144 bytes, OTA headroom 141,456 bytes. Image integrity and embedded identity are verified. SHA-256: `c3bbc46ccfa6b2165f6a4d116f3439f0f85a6a6decbfcdab796017cdb17c2e95`. This build completes the compile gate for the publisher-numbering block; it does not contain the subsequent reader-command work.

## C3 reader commands in progress (280 to 282)

The command dispatcher uses existing CrossPoint reader, statistics, network and settings screens. Default bindings inherit CrossPoint. Imported legacy IDs are retained explicitly; Menu and Back use their original separate mapping from Power. Holds fire at 600 ms, 1,000 ms and 400 ms respectively. Settings are serialized on all boards, but only the active C3 EPUB reader interprets them. S3 Home/touch, TXT and XTC retain CrossPoint controls. Unsupported clipping IDs stay blocked by migration and are absent from the popup choices.

Commands cover sleep, page turn on Power, full refresh, fonts, guide/focus reading, bookmarks, KOReader synchronization, completion, statistics, screenshot, automatic-turn interval, file transfer, tilt, reader dark mode, footnotes, browser, Calibre, Join Network, Hotspot and X3 reinforcement. Network entry replaces the reader before enabling Wi-Fi. Statistics releases pagination before its screen allocation. Shared constant option tables reside in flash; no command parsing or heap allocation is added to page-turn loops. Screen allocations occur only after an explicit command and fail without discarding the reader when allocation fails.

Sleep is requested through a main-loop flag, avoiding recursive destruction of the active reader during its input handler. A Power hold consumes its later release even after opening another screen. A configured short Sleep waits for release when a custom long action exists. The interval picker returns to its actual origin on cancellation. Explicit completion feeds the existing recent-book and finished-folder policies; the separate move-transaction audit is still open.

X3 280 validates the three real bindings (Menu statistics, Back Join, Power refresh), additional font/guide/focus toggles, bookmark, missing-credential sync settings, completion, screenshots, intervals, transfer modes, tilt, dark mode, browser, reinforcement, Ignore and short Power statistics. Pro 281 keeps Home, touch and menu captures pixel-identical with the imported C3 bindings enabled or absent. English statistics and guide captures were reviewed. The chord source audit found that Down could outlive the Power-only guard; 282 holds both keys until both are up. Screenshot feedback restores CrossPoint's monochrome snapshot, so an anti-aliasing pixel difference is not evidence of a page turn. The guard verifier instead checks the saved chapter/page and render count. The small option dialog caps at sixteen choices; 283 routes these four settings to the existing scrolling-list component. Guard and list checks are being completed before the next Tiny gate.

Candidate v25 has zero blocked real global keys, zero omitted-default blockers, all seven book profiles and twelve recents. The 99 Python checks pass, including raw legacy command IDs, invalid values, and rejection of unported clipping. This is a conversion candidate only: unresolved positions, gallery order, complete transactional activation and physical acceptance remain open.


The command checkpoint now has 24 distinct passing simulator cases, collected from `reader-actions-x3-280`, `reader-actions-x3-280-b` and `reader-actions-x3-283`. Six additional 283 scenarios cover Menu sleep and resume, short Power sleep with a custom long binding, both screenshot-button release orders, disabled Menu hold and inherited CrossPoint Menu hold. Five 283 settings cases select Menu, Back, long Power and short Power through the actual scrolling list, plus cancellation without a write. The Join Network selection below the original popup limit was visually reviewed in English. Candidate v25 was generated twice with identical contents and unchanged source hashes. Tiny 284 succeeded: application 6,403,763 bytes, static RAM 58,776 bytes, image 6,417,840 bytes and OTA headroom 135,760 bytes. SHA-256: `c6ee096e7486fbc3e29158386b9f78a9ee18ec7e2ad424391cbbc636c8e909d7`.

## Book-path consistency work (285 onward)

CrossPoint deletion and remove-from-recents remain the user-facing operations. The automatic finished-book move, file-browser rename, and portal move/rename now share a recoverable intent. It covers the book, cache (including progress/statistics and unknown data), bookmarks, per-book profile and gallery. Destination data prevents a move rather than being overwritten. Recent-book path persistence now reports failure and restores RAM state; finished-history collisions no longer discard another entry.

Two checked JSON snapshots preserve the complete pre-move recents and application state. Boot restores them before loading those stores and replays a valid intent. Failed or ambiguous recovery pauses storage navigation until a deliberate restart/recovery; malformed intents never authorize renames. The record lives only during an explicit operation, with no per-page I/O or memory allocation. Scope includes both C3 and S3.

The production mover passes 54 in-memory SD scenarios under address/undefined sanitizers, covering 15 mutation points, torn writes, retries, destination collisions, invalid paths and corrupt intent. Separate production recent-store checks cover all eighteen removal/rename positions and failed-save rollback. The X3 285 automatic completion/move scenario preserves payloads. Its Web scenario exposed concurrent main-loop error handling during a normal in-flight request; 286 separates in-progress from failed operations. Web, boot replay, Tiny and hardware validation are not yet complete at this checkpoint.

## Book-path validation checkpoint (287 to 289)

X3 288 passes automatic completion and relocation, portal move/rename, destination-state collision refusal, and boot replay after one companion directory moved and state.json was truncated. Pro 287 passes the portal and boot-replay cases. Each verifies unchanged book bytes, bookmark data, profile bytes, unknown cache data and gallery bytes. The journal is retired only after metadata persistence. Tests of production FinishedBooksIndex also preserve reconstructed history before a cache move and reject a different destination history without deleting either entry.

Tiny 289 succeeds: application 6,408,495 bytes, static RAM 58,784 bytes, image 6,422,576 bytes and OTA headroom 131,024 bytes. Embedded identity and image integrity are verified. SHA-256: `bd611a22b44a71641e7515e7190343e1f85d0702688f90608f885405d2002d74`. The 102 Python checks pass, including three catalogue-manifest cases for cross-platform ordering, changed content and duplicate records. No installation or physical hardware acceptance is implied.

Physical follow-up: on an expendable SD copy, open a bookmarked/profiled book with a per-book gallery on X3, finish it with the read-folder option, reopen the moved book and verify progress, statistics and bookmark navigation. On Pro, repeat the portal rename/move and reopen through touch. Retry after a controlled interruption on a disposable copy; a corrupt/ambiguous intent must show an error and retain its snapshots instead of enabling ordinary storage operations. Holding Power for two seconds requests a recovery reboot. Do not induce power loss on the original card.

## Reference preservation check

After the book-move integration, all 5,747 files in the frozen YACP reference still match their recorded lengths and SHA-256 hashes. The original and interrupted CrossInk worktrees remain registered. No worktree or ambiguous experiment was removed, and no original card was written. The source-preservation report is `qa-artifacts/reference-preservation-after-book-moves.json`.

## Aligned firmware checkpoint (289 to 294)

All six profiles compile successfully with the retained features and common book-move recovery. Every build has its own incremented version. Only offline migration/verification scripts and documentation changed while the later builds ran; firmware sources remained fixed. The source checkpoint and aggregate image verification are retained in `qa-artifacts/final-source-checkpoint-292.json` and `qa-artifacts/aligned-builds-289-294.json`.

| Hardware | Variant | Version suffix | Application bytes | Static RAM bytes | Image bytes | OTA headroom bytes |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| X3 / original X4 | Standard (`tiny`) | 289 | 6408495 | 58784 | 6422576 | 131024 |
| X3 / original X4 | XLarge | 290 | 6327265 | 58720 | 6341344 | 212256 |
| X4 Pro | Standard | 291 | 6472046 | 102656 | 6477056 | 76544 |
| X4 Pro | XLarge | 292 | 6391042 | 102592 | 6396064 | 157536 |
| X4 Classic | Standard | 293 | 6450935 | 102528 | 6455952 | 97648 |
| X4 Classic | XLarge | 294 | 6369815 | 102464 | 6374832 | 178768 |

Versions are `1.7.2-yacp.<suffix>`. The smallest remaining image margin is 76,544 bytes on Pro Standard. Static RAM is a linker measurement, not runtime heap acceptance. These builds are local test artifacts; none was flashed or published.

At this historical checkpoint, 105 Python tests passed and complete-copy assembly was pending. The current complete-copy checkpoint at the top of this document supersedes that migration status; physical acceptance remains open.


## Complete-copy firmware checkpoint (297 to 302)

All six profiles compile with the TXT source-offset correction and the retained port. Versions are `1.7.2-yacp.<suffix>`. Each image has verified embedded version, board/variant, chip ID, segment checksum, SHA-256 and OTA size. Firmware sources were held fixed during the sequence; only offline tools and documentation changed. Evidence: `qa-artifacts/aligned-builds-297-302.json`.

| Hardware | Variant | Suffix | Application bytes | Static RAM bytes | Image bytes | OTA headroom bytes |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| X3 / original X4 | Standard (`tiny`) | 297 | 6408753 | 58784 | 6422832 | 130768 |
| X3 / original X4 | XLarge | 298 | 6327523 | 58720 | 6341600 | 212000 |
| X4 Pro | Standard | 299 | 6472318 | 102656 | 6477328 | 76272 |
| X4 Pro | XLarge | 300 | 6391326 | 102592 | 6396336 | 157264 |
| X4 Classic | Standard | 301 | 6451211 | 102528 | 6456224 | 97376 |
| X4 Classic | XLarge | 302 | 6370087 | 102464 | 6375104 | 178496 |

The smallest OTA margin is 76,272 bytes on Pro Standard. Static RAM is a linker measurement, not runtime heap validation. No image was installed. The first proposed hardware artifact is `qa-artifacts/bin/YACP-1.7.2-yacp.297-tiny.bin`, for the Standard X3 trial on a disposable copy of the prepared ESP32 card. Keep original G: unchanged.

The complete migration software report is `qa-artifacts/complete-migration-acceptance-2026-10-02.json`. The final read-only check of all 1,279 source-card files reports zero size/hash differences. The SDK remains clean at the pinned commit. The original YACP and interrupted CrossInk worktrees remain registered. No commit or publication was made. Hardware acceptance, including real SD timing, deep sleep, display behavior, heap, Wi-Fi and Pro touch/frontlight, remains required.


## On-reader migration firmware checkpoint (314 to 319)

All six profiles compile with the on-device importer, update-time button chooser, X3 defaults and canonical YACP Home logo. The 828-file source manifest was held fixed, except for the required embedded build-version increment. All image identities, checksums and OTA sizes are verified. These are test binaries; physical acceptance remains pending.

| Profile | Build | Application bytes | Static RAM bytes | Image bytes | OTA headroom bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| tiny | 314 | 6460037 | 58856 | 6474112 | 79488 |
| xlarge | 315 | 6378787 | 58792 | 6392864 | 160736 |
| x4pro | 316 | 6522750 | 102720 | 6527760 | 25840 |
| x4pro-xlarge | 317 | 6441638 | 102656 | 6446656 | 106944 |
| x4c | 318 | 6501291 | 102592 | 6506304 | 47296 |
| x4c-xlarge | 319 | 6420167 | 102528 | 6425184 | 128416 |

Evidence: `qa-artifacts/aligned-builds-314-319.json`. The smallest remaining OTA margin is 25,840 bytes on Pro Standard. Standard X3 build 314 was copied to G: for manual installation; no flash, commit or publication was performed.
