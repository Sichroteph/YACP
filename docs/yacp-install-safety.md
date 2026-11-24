# YACP migration fix and PC installation safeguards

## Scope

The 2026-10-17 incident followed an older YACP boot after a successful migration. The completed-migration marker no longer described the active card: legacy progress had been rewritten and a CPV1 Wi-Fi envelope had been saved as a native password.

The firmware change only corrects migration. Added preinstall hooks, the `/api/firmware/preflight` endpoint, firmware data-schema requirements and automatic preinstall snapshots have been removed. The flasher, board scanner and web server are restored to their pre-change implementations. GCC size pragmas, `noipa` attributes and the path/JSON helpers introduced to fit those safeguards have been removed.

## Firmware migration

A completed migration now rechecks active legacy inputs before the normal settings and credential loaders can resave them. A valid native password stays unchanged; a validated CPV1 envelope is unwrapped using the device's factory identity. Legacy EPUB pages are mapped to a unique source-text passage using the existing matcher.

Repeat repair retains the complete prior active store, prepares an inactive copy and resumes interrupted conversions. Per-book source receipts prevent an unchanged old record from rewinding later native reading. Conflicting useful positions without a known source receipt stop conversion with the originals retained. The first migration keeps its existing transaction and rejects missing or ambiguous position anchors.

An ordinary deep-sleep resume skips library inspection after a successful migration check. Cold restart checks again. This changes no page-turn path. The existing fallible boot workspace is reused and released before reading; no background task or framebuffer is added.

## PC-only safeguards

Use the canonical `scripts/deploy_yacp.py`. It checks the paired canonical build report, closed build ticket, current source fingerprint, embedded version/variant/chip/hash and the migration regression evidence for that source. No firmware marker is required.

Network preparation uses the existing `/api/status`, `/api/settings`, `/api/files` and `/download` APIs. The existing `showHiddenFiles` setting must be enabled so a listing cannot silently omit hidden data. The tool copies the active store to the PC and compares its files against a second read. Any inaccessible file, changed data, incomplete listing or changed device identity stops transfer. A protected dotfile which the server refuses to download requires mounted-card preparation. The tool changes no reader setting and creates no device-side backup.

Provide the known factory MAC to validate Wi-Fi data. The PC audits mixed legacy/native positions using the visible library paths and conversion receipts. Unreconciled positions, legacy TXT/XTC data and pending repairs stop deployment for review.

```text
python scripts/deploy_yacp.py qa-artifacts/bin/YACP-<version>-tiny.bin --device <ip> --factory-mac <known-factory-mac> --backup <new-private-folder>
```

Without `--apply`, this only checks and backs up. Add `--apply` for a requested installation. A mounted card instead uses `--card <root> --model <model> --factory-mac <known-factory-mac>`: every readable file and empty directory is preserved privately before audit; applying only copies the descriptively named firmware and paired report. A previously repaired card whose native position has advanced may need a reviewed baseline receipt. Never clear caches or reset migration markers to force conversion.

No installation or publication follows from a build or test. A lost final install response remains unconfirmed. There is no status poll after an acknowledged reboot. Existing image-integrity and hardware checks in the original firmware flasher remain unchanged.

## Regression evidence

The native regression executes first migration, a legacy rewrite after completion, three render modes, later native advancement, a second legacy return and six interrupted-repair boundaries. Invalid credentials, missing text anchors and conflicting positions must preserve originals. The isolated real Congo incident must recover spine 38 and source offset 949. The complete first-migration fixture also checks 78 settings, 11 positions and five profiles. Real credential envelopes are adapted only in the simulator fixture to its fixed factory identity; original archives remain unchanged.

Host tests exercise PC refusal and backup ordering. The C++ scanner test covers the pre-existing board-tag behavior only. Physical acceptance still requires Congo reopen, sleep/wake and restart, then joining the saved network on the X3. No cache reset is needed. Simulator results do not establish the radio handshake or physical SD power-loss behavior.

## Removal verification, 2026-10-18

Eight flasher, board-scanner, web-server and migration-helper files match their private pre-change copies byte for byte. The current Standard binary contains neither the removed data-schema marker nor the preflight endpoint. The 158 Python tests, 15 native repeat-migration scenarios, initial migration's interruption tests, mixed-credential checks and real-data first-migration fixture pass. The live simulator confirms the removed endpoint returns 404 and the PC uses only existing APIs, refusing hidden or unreadable data. No reader was contacted.

| Profile | Build | Image bytes | PC image acceptance |
| --- | --- | --- | --- |
| Standard, X3/original X4 | `1.7.2-yacp.566` | 6,501,184 | Accepted, 52,416 bytes of OTA margin |
| Pro Standard | `1.7.2-yacp.567` | 6,555,904 | Refused, 2,304 bytes beyond the OTA slot |

The Pro compilation itself succeeds; its resulting image is too large. No new size tuning was introduced after removal. Classic and XLarge profiles have not been rebuilt for this removal. This is not general-release or hardware acceptance. Evidence: `qa-artifacts/install-safety-removal-result.json`. No firmware was installed and no commit was made.
