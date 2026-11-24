# Single-source YACP recovery, 2026-10-17

YACP prioritizes battery efficiency, responsive reading and avoiding unnecessary work on the reading path.

The CrossPoint-based project is the single active YACP implementation. The earlier standalone YACP and interrupted
CrossInk port are preserved recovery references. They must not receive implementation work, builds or installations.
The current local authority is `C:/Users/Totofaki/.codex/worktrees/yacp-crosspoint/Crossink`, branch
`chore/sync-crosspoint`. The policy is in `.yacp-source.json`; `scripts/check_yacp_source.py` verifies its Git ancestry,
SDK revision and registered worktree. The authority record lives in shared Git metadata, outside firmware sources.

Follow-up: the first physical X3 test exposed a pre-existing completed-migration re-entry issue after a legacy firmware boot. See [the card diagnosis and recovery](yacp-x3-recovery-2026-10-17.md). Compilation and synthetic migration checks did not cover that mixed-store lifecycle.

## Hardware matrix at the time of recovery

YACP 2.0.0 uses the three Standard profiles from this table. XLarge settings and artifacts still apply to earlier releases.

| Reader | MCU | Standard, recommended | XLarge | Specific behavior |
| --- | --- | --- | --- | --- |
| X3 | ESP32-C3 | `tiny` | `xlarge` | Detected X3 controller, RTC/tilt and X3 Magic Flash gates |
| Original X4 | ESP32-C3 | `tiny` | `xlarge` | Runtime X4 detection and physical controls |
| X4 Classic | ESP32-S3 | `x4c` | `x4c-xlarge` | Classic physical controls and board capabilities |
| X4 Pro | ESP32-S3 | `x4pro` | `x4pro-xlarge` | Touch, Home key and frontlight |

Three hardware images cover four readers because the C3 firmware detects X3/original X4. Standard and XLarge made up
six build profiles at the time of recovery. Each diagnostic compilation reserves its own incremented version, including failed attempts.
These counters identify builds and must not be used to infer which historical codebase is current.

## Evidence and cause

- The user-approved reconstruction plan in [YACP - Crossink - SYNC](codex://threads/01a0f393-228c-7fa3-9024-fe9df0cfaf6d)
  made CrossPoint 1.6.5 the direct base. Its UI, touch conventions and text layout were authoritative; the old YACP and
  interrupted CrossInk trees were to remain references. Only distinct useful YACP behavior was to be adapted.
- The outer `AGENTS.md` still explicitly selected nested `YACP/` as the default implementation/build target. Later
  discovery notes mentioned the CrossPoint worktree without removing that default. The instructions contradicted the
  approved migration and were not a new user decision to maintain two product implementations.
- The roots had separate histories and large uncommitted changes. A patch in one root did not reach the other.
  Builds used similar version counters without recording or enforcing the actual source authority.
- The profile chooser was removed in the CrossPoint tree in [YACP - parameters](codex://threads/01a1027f-9d90-73a3-8f84-31c440e62880).
  The later local `.501` firmware still contained it. Local `.502` compiled with its removal but was not installed
  successfully during that attempt. This explains the panel's return without reversing the user's decision.
- October 16 settings hints and Magic Flash changes initially went into legacy YACP and were subsequently adapted
  into the CrossPoint tree. The newest two-mode X3 display policy supersedes the older three-option menu.
- The old Linux simulator mirror also retained removed source files because its copy process did not remove them.
  A fresh mirror exposed incomplete GfxRenderer copying. The build wrapper now copies that complete module and verifies
  that the simulator's application sources exactly match its reserved source ticket.

## Preservation and audit method

The private recovery directory is `XTEInk/YACP-archives/2026-10-17-consolidation-unique-source`, outside both project
trees and Nextcloud. It contains complete worktree copies, shared Git metadata, binary working/staged diffs, worktree
registries, individual file lengths/SHA-256 values and raw Windows reparse records. Every copied regular file is hashed
at source and destination. The source-preservation checkpoint contains 108,689 files and 4,163,613,574 bytes with no
errors. The complete preservation then finished successfully: 418,599 regular files, 114,689,800,243 bytes,
13 raw reparse records and zero errors. `result.json` records these totals and the manifest SHA-256. No source history
was rewritten and no worktree was deleted. A separate final checkpoint retains the repaired, uncommitted source,
updated entry guides and accepted images; it does not overwrite the original snapshot.

Original authored files were preserved before edits. The later full-copy pass also collected new task evidence and
files created while build/cache preservation continued. `audit/pre-edit-file-index.jsonl` identifies the original
checkpoint's exact file set, so later additions are not mistaken for pre-existing implementations. The repair diff
uses that index and checks each compared original against its saved hash.

The October 1 frozen YACP source remains the pre-port comparison baseline. The audit found 137 normalized file changes
between that snapshot and the current standalone checkout. This is a file count, not a count of missing features.
Generated translations, build products, unused source assets, documents and retired Go files are included in that count.
The release-preparation branch was checked separately: 30 application C++/header files differ from the frozen snapshot.
Their behavior belongs to the font/metadata, dictionary, statistics, reader/layout, display, settings and OTA blocks
already covered by the port matrix or to superseded legacy controls. File differences alone do not authorize replacing
CrossPoint implementations. The private audit retains the exact changed-file lists and individual decisions.

Use `docs/yacp-port-matrix.md` for the original 52 behavior groups and their historical evidence. The present audit
adds the post-snapshot differences and current source/build controls. Earlier successful builds and simulator checks
remain historical evidence rather than proof of current physical acceptance.

## Reconciled behavior

| Behavior | Recovery decision |
| --- | --- |
| Top-center reading clock | Preserve the current option and preview; pin actual glyph ink to the oriented usable top edge, remove the extra four-pixel height and use one font line for reservation. EPUB/TXT layout uses that inset; XTC keeps a small cleared overlay. |
| Classic, Lyra Extended and RoundedRaff Home menus | Recover smaller cover tiles and bound menu geometry using actual row count, font height and usable bottom. Optional rows shrink the cover further; short rotated layouts paginate. Drawing and Pro touch use the same geometry. |
| Update-time profile chooser | Already removed in CrossPoint. Preserve that removal and existing settings. Fresh X3 defaults use Magic Flash every text page with smoothing off. Legacy migration may still read old markers to identify saved data. |
| Settings groups and returning selection | Retain the CrossPoint port's existing grouped settings and parent-row return fix. Local class names/translation IDs are not copied over the newer UI. |
| Refresh hints, Magic Flash and allocation fallback | Already adapted. Keep the later Magic Flash/Classic policy, required cleanup paths and BW completion after failed grayscale scratch allocation. |
| YACP Home design | Keep the later approved cover-free typographic Home. The temporary legacy cover-based dashboard and its extra cover reads are superseded. Other CrossPoint themes remain available. |
| Network name | Recover `yacp.local` for File Transfer and Calibre discovery from the local implementation. |
| Explicit-CA TLS verification | Recover peer verification, checked CA loading and hostname validation in the SDK. Explicit insecure callers retain their existing choice; setting a CA clears a previous insecure choice. |
| RTC write verification | Preserve the existing CrossPoint-tree fix as a reproducible SDK patch rather than an undocumented dirty submodule change. |
| JPEG/wolfSSL dependency preparation | Preserve whitespace-tolerant JPEG patches and honor the selected library-dependency directory, allowing isolated validation outputs. |
| Statistics, progress, per-book profiles/galleries, fonts, dictionary and OTA | Retain their existing CrossPoint adaptations and data importer. Their older source implementations remain archived; avoid duplicate reading-path work. |
| Minimal/carousel legacy themes | Keep CrossPoint's theme set plus YACP, as specified in the reconstruction plan. Preserve old theme sources in the archive. |
| Nearby reading positions, text clipping/export and cross-book annotations | Excluded by the user during reconstruction. Keep their original data archived. Nearby statistics remains included. |
| Personal Dragon Go Server | Retired by explicit user decision. Its sources/evidence remain archived and its environments are absent from `all-local`. |

The viewport height is already part of `ReaderRenderSpec` and section-cache validation. Changing clock reservation
rebuilds incompatible cached layout automatically while retaining the visible-text progress anchor. No cache binary
layout was changed and no real book cache was cleared for this repair. The new Home geometry uses a 24-byte value
object on the stack and adds no heap allocation or persistent preference.

## Build enforcement

1. `python scripts/check_yacp_source.py` verifies the registered CrossPoint worktree. A fresh clone can be explicitly
   registered once with `--register`; that command refuses to replace an existing authority with a sibling worktree.
2. `python scripts/build_yacp.py <profile> --pio <executable>` takes an OS lock in shared Git metadata, applies the
   reviewed SDK patches, fingerprints authored inputs and reserves a unique version and single-use build ticket.
3. The first PlatformIO pre-hook checks the source, profile, version and ticket. Direct compilation without the wrapper
   fails. Archived YACP configurations expose only a native refusal target. Explicit former firmware environments fail before
   resolving dependencies, and the default refusal hook points to the canonical project.
4. Simulator builds use `--wsl-mirror <mirror>` and compare the application source set and hashes before generation or
   compilation. The ESP32 toolchain may re-enter within the same active wrapper lease while rebuilding its SDK; the
   session closes on return, so later ticket replay and dead-owner reuse fail. Stale files are errors; references are retained and a fresh mirror is used instead of deleting them.
5. Successful builds are accepted only if inputs are unchanged afterward and the guard consumed their ticket.
   Firmware image version, variant, chip/board tags, checksum, digest and OTA size are checked before named artifacts
   and ELF symbols are retained. Each report includes Git/SDK identity, source fingerprints and the actual build ticket.

Static font headers and their `all.h` catalogue are fingerprinted as direct compiler inputs. Only the known I18n,
web, runtime-logo and YACP-font outputs regenerated by the pre-build scripts are excluded. Unknown generated-looking
filenames and binary source assets remain inputs. The final review caught the earlier broader exclusion; the matrix
was repeated after this correction so accepted image reports carry the complete input fingerprint.

`all-local` builds the six supported firmware profiles sequentially. It never includes retired Go profiles. The wrapper
does not install or publish firmware. The shared PlatformIO packages must not be modified by simultaneous builds.
Before transfer or installation, the agent guide requires the paired canonical build report, a matching current source
fingerprint and verification of the binary identity and digest. A higher version counter alone is not acceptance.

The YACP CI workflow registers its fresh checkout, checks source/workflow contracts and migration helpers, then calls
the same sequential `all-local` wrapper for the six profiles. It retains descriptively named images and build evidence
with read-only repository permission. Diagnostic ranges include the workflow run and attempt. The inherited upstream
release, font-publishing and PR-comment jobs are restricted to the CrossPoint repository. Preparing this workflow does
not execute GitHub Actions or change branch-protection settings. Those remote checks require a later authorized push.

## Validation and physical acceptance

The recovery verification records are under `qa-artifacts/consolidation-*`. Source-authority tests cover missing markers,
wrong roots/profiles/versions, changed inputs, authority replacement, ticket reuse and direct compilation. The standalone
Home geometry checker covers 756 combinations of screen height, bottom insets, row heights, row counts and selection.
English simulator captures check the three affected themes, settings entry and Pro touch, plus EPUB/TXT/XTC clocks on
all four models. On original X4 without an RTC, the tests verify no false clock or reserved line, including a pixel
comparison against the hidden-clock setting. Firmware results and final image identities are recorded separately from simulator results.

The Python suite passed all 140 cases, including 16 source-authority checks and six workflow-policy checks. The native
Home geometry check passed 756 combinations. The final simulator matrix passed 24 cases across the four readers.
Additional regression records cover per-book sleep galleries with preserved original images, sleep-gallery HTTP
validation (including 13 invalid paths and two ETags), and idempotent SDK patch replay from the pinned clean files.
After the final source-guard correction, native X3 build `1.7.2-yacp.536` passed the three menu cases and all three
reader-format cases again. Each EPUB/TXT/XTC clock capture has its first visible clock pixels at row zero. Its source
fingerprint matches the six accepted firmware builds, and its build session is closed.

The Pro Standard image `1.7.2-yacp.532` is 6,551,104 bytes against the configured 6,553,600-byte OTA partition, leaving
2,496 bytes. The image length comes from the built file and the limit from the partition configuration. No runtime RAM
measurement is inferred. It fits the configured layout, but the margin is very small. The image-size guard remains
mandatory. This consolidation does not change partitions.

### Accepted diagnostic firmware matrix

All six accepted images were rebuilt after the static-font input guard correction. Their authored-input fingerprint is
`13648f5529bfb1037fe7c3bbbd93e003d100ee489afd72152748890a66aed42c`. Each image also has its own SHA-256, paired JSON report and ELF symbols under `qa-artifacts/bin/`.
No physical installation or hardware acceptance was performed in this consolidation.

| Profile | Diagnostic version | Image bytes | Configured OTA headroom, bytes |
| --- | --- | ---: | ---: |
| `tiny` | `1.7.2-yacp.530` | 6,496,320 | 57,280 |
| `xlarge` | `1.7.2-yacp.531` | 6,415,056 | 138,544 |
| `x4pro` | `1.7.2-yacp.532` | 6,551,104 | 2,496 |
| `x4pro-xlarge` | `1.7.2-yacp.533` | 6,469,936 | 83,664 |
| `x4c` | `1.7.2-yacp.534` | 6,528,352 | 25,248 |
| `x4c-xlarge` | `1.7.2-yacp.535` | 6,447,248 | 106,352 |

### On-device acceptance

Physical acceptance remains required. On each reader, install its matching verified variant when requested, confirm
existing settings and the absence of a profile prompt, reopen the reference book at the retained passage, turn pages,
check statistics and clock/menu placement, then exercise sleep/wake. On Pro also verify Home, touch hit targets and
frontlight. Test display-waveform quality and current draw on the physical controller; simulator pixels cannot establish
them. Keep original cards and recovery snapshots intact. No installation or release is implied by this audit.

For the recovered explicit-CA TLS path, use a controlled HTTPS endpoint: the trusted certificate with its matching
hostname should connect, while an unknown CA or mismatched hostname should fail with a verification error in the log.
The SDK patch replay and successful builds verify source integration; an actual device TLS handshake remains to test.
