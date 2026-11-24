# X3 mixed-store recovery, 2026-10-17

The user reported that Congo resumed at the beginning and saved Wi-Fi connections failed after testing the consolidated firmware. On-device acceptance is pending after this card repair.

## Evidence

The X3 previously reported `1.7.2-yacp.501-tiny` and serial `X3AW01EN26061209783`. The new `.530` Standard image was transferred and installation requested; its final HTTP response and post-reboot version were not captured. The card contains the exact verified `.530` binary. This file alone is not proof of the running image.

The card retains `/.yacp-migration-v1/complete`, an earlier original store, native `settings.json`, and a legacy `crossink-settings.json` naming `.501`. `LegacyMigration.cpp:673` skips conversion when that completion marker exists, before the separate Wi-Fi converter can run. This is a return from native firmware through legacy firmware with changed data, not a new first-boot migration.

Congo has two active cache identities for the exact path `/Congo - Michael Crichton.epub`:

- Legacy FNV64 `epub_2701754190095167538`: spine 38, page 2 of 33; backup is page 1. The first cached line is `Karen Ross ne se retourna pas`.
- Native ESP32 GCC32 `epub_3965984633`: spine 0, page 0, visible offset zero.

The legacy line occurs exactly once in spine 38 of the unchanged EPUB, at visible-text codepoint offset 949. A ten-byte native progress record retains that passage independently of font pagination. The accepted native executable `.536`, sharing the `.530` authored-source fingerprint, resolves it to page 1 starting at offset 420 and preserves it after closing and reopening. The containing page starts earlier than the source line; no subsequent text is skipped.

Both saved Wi-Fi rows contain a valid same-device CPV1 envelope. The native store has re-saved its 27-byte envelope with length/CRC as though it were the password. Explicit factory identity from the earlier X3 USB recovery log verifies both envelope checksums. Their actual passwords are 19 bytes. No password or obfuscated credential is included in this report.

`finished_books.bin` is byte-for-byte identical to the earlier original. L'Homme Terminal was already marked finished on 2026-08-25, with 20,484 recorded seconds. Its historical cause is unknown. The repair does not change completion history.

## Preservation and repair

A complete private copy of the card contains 1,637 verified files, 1,302,043,806 bytes, with no copy error. Location: `C:/DEV/Projets/C-Pebble-Ubuntu/XTEInk/YACP-archives/2026-10-17-x3-recovery-after-530/card-original`. The manifest, diagnosis, candidates, simulator evidence and intervention report are alongside it, outside Nextcloud.

Four live files were repaired after rechecking every original source hash: native Congo `progress.bin`, `stats_v6.bin`, `stats_v6.bin.bak`, and `.crosspoint/wifi.json`. The prior live versions were renamed with `.pre-recovery-530` on the card and remain in the complete PC snapshot. The recovered Congo statistics contain 23,111 seconds and 600 recorded page turns. The native post-reset diagnostic statistics are retained in the backups rather than merged into prior reading history.

The other 1,633 original files, all original migration data and all books remain byte-identical. No deletion or firmware flash was performed during card recovery.

## Hardware acceptance and remaining software work

Safely eject the card, put it back in the X3 and restart. Confirm Congo opens at the recovered passage, close/reopen it and test sleep/wake. Join the saved network and report its displayed IP. A working radio connection is not proved by offline credential checks. Keep all recovery originals.

The first migration's completion marker cannot serve as perpetual proof that later legacy-written files are compatible. Device-specific SD backup and mixed-store preflight are now mandatory before installation. Firmware changes for repeat conversion require a separate transactional design and regression coverage; this intervention repairs the real card without resetting or replaying its original migration.

Follow-up implementation: [installation and repeat-migration safety](yacp-install-safety.md) describes the automatic guards and transactional repeat conversion. The manual card repair above remains a separate, preserved event.
