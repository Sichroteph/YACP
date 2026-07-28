---
title: Nearby Position Sync
nav_order: 8
---

# Nearby Position Sync

CrossInk can share the current EPUB reading position between two nearby X3/X4 readers running CrossInk. The sync is direct reader-to-reader over ESP-NOW; it does not use WiFi, a server, an account, or KOReader's online sync service.

Nearby Position Sync is separate from [Reading Stats Sync](./reading-stats-sync.md). Stats Sync shares all-time reading totals. Nearby Position Sync shares one in-book location for the currently open EPUB.

## What Gets Synced

Nearby Position Sync shares the current location in the open EPUB:

- book identity
- overall book percentage
- section / spine index
- page number within that section
- paragraph or anchor details when available

It does not sync files, bookmarks, clippings, reading stats, KOReader server data, WiFi settings, OPDS servers, or reader options.

The receiving reader only applies the position after you confirm it.

## Requirements

- Both readers must be running CrossInk.
- Both readers must have the same EPUB open.
- Both readers must be near each other.
- Nearby Position Sync must be run on real hardware; it is not available in the simulator.

The book check uses the same document identity style as KOReader sync. If KOReader sync is configured to match by filename, Nearby Position Sync uses the filename-based document ID. Otherwise it uses the EPUB document hash.

## How To Use It

1. Open the same EPUB on both readers.
2. On the reader with the position you want to copy, go to that page.
3. On both readers, open the in-book menu and select **Nearby Position Sync**.
4. Press **Share** on the reader that is already at the correct page.
5. The other reader shows the nearby position beside its current local position.
6. Press **Select** on the receiving reader to apply the nearby position, or **Back** to leave the current position unchanged.

Only one reader needs to press **Share**. The other reader should stay on the Nearby Position Sync screen and wait for the shared position.

## What Happens On The Device

Before starting the sync, CrossInk saves the current reader progress. The sender then broadcasts the current EPUB position over ESP-NOW. The receiver checks that the incoming packet is from a compatible CrossInk nearby-position protocol and that the book identity matches the EPUB currently open on the receiver.

If the book matches, the receiver maps the shared KOReader-compatible position back to CrossInk's local section and page model. When paragraph, list-item, or anchor details are available, CrossInk uses them to refine the target page.

After you press **Select**, CrossInk writes the received position to the normal EPUB progress file and returns to the reader at that position.

## Common Messages

**Different book. Position not synced**

The two readers are not opened to the same EPUB according to the current document matching method. Open the same book on both readers and try again.

**Version mismatch. Position not synced**

The other reader is using an incompatible nearby-position sync protocol. Update both readers to compatible CrossInk builds.

**No nearby reader found** or **Position sync timed out**

The readers did not find each other within the sync window. Keep both readers close together, leave both on the Nearby Position Sync screen, and press **Share** on only the reader that should send its current position.

**Nearby position sync is not available in simulator**

This feature depends on ESP-NOW hardware support, so it only works on a real X3/X4 reader.
