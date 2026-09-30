YACP aims to make reading responsive and economical with battery, memory, storage, display, and network work.

# YACP 1.8 beta 2: long chapters, update checks, and community fixes

This beta brings together the changes made since [YACP 1.8 beta 1](https://github.com/Sichroteph/YACP/releases/tag/v1.8.0-yacp.1). It is intended for readers who can test difficult books, dictionary selection, reading statistics, Smart Sync settings, and the updater on real X3 or X4 hardware.

## Added

- **Smart Sync in the web portal:** KOReader's optional Sync Behavior setting can now be changed from the browser as well as on the reader. The selection is saved with the KOReader configuration and survives a restart. Manual comparison remains the default.

## Changed

- **More words selectable for dictionary lookup:** the EPUB dictionary selector now covers up to 240 words on a dense page, matching the clipping limit. Its existing check of the largest free memory block remains in place, so a device under severe memory pressure can still retain fewer than 240 words.
- **A clean start for online update checks:** choosing Check for Updates now silently restarts the reader and opens the update screen again before connecting to Wi-Fi. It then releases SD font memory before network work. This is intended to reduce the memory pressure seen during HTTPS negotiation after a reading session. One complete update through this path succeeded on an X3 in a staging test. The original updater issue remains open while readers test this beta on other devices and networks.

## Fixed

- **Reading Rhythm duration:** the daily and weekly views no longer stop accumulating a day's reading time at 4 h 15. The daily history file now stores 16-bit minute values and automatically reads older version 1 files. The file uses 730 additional fixed bytes in memory and on the SD card. Time already discarded by the old cap cannot be recovered.
- **Very long Chinese chapters:** long, unbroken text is now split within the configured layout limit even when a single XML callback contains several internal fragments. This prevents the temporary token buffer from growing past that limit and blocking adaptive pagination after its first pages. A constrained simulator scenario completed 99 pages; the reporter's original X4 chapters still need confirmation.
- **White-circle character:** when the active font lacks `○`, YACP draws a small fallback glyph instead of a replacement diamond. This does not replace missing Chinese characters in general.

## Validation requested

1. **Long Chinese EPUB chapters:** clear the affected book's `.crosspoint/epub_<hash>/` cache, then retry the chapters that previously stopped around page 3. Report the device model, font and size, style mode, and whether replacement diamonds or `Chapter needs too much memory` remain. No private test book is included in this release.
2. **Dictionary:** open a dense EPUB page, select a word near the end of the page, open its definition, return to selection, and repeat. The 240-word stress page passed in the simulator and on an X3, including selection of its final word.
3. **Reading Rhythm:** read for more than 4 h 15 across one calendar day, then check the daily and weekly totals. Existing version 1 history should migrate automatically on the next save.
4. **Smart Sync:** change Sync Behavior in the web portal, restart the reader, and confirm that the same choice appears in KOReader settings on the device.
5. **Online update check:** after reading, choose Check for Updates. The reader should restart silently, reopen the update screen, and reach the release check. If it fails, report the device model, network type, the displayed message, and a serial log if available. A successful check alone does not confirm a successful firmware installation.

## Credits

- **walloftexts** supplied detailed Chinese EPUB diagnostics, private test books, and reproduction steps that exposed the remaining pagination limit. Their books and attachments are not redistributed here.
- **[OzzyMcBean](https://github.com/Sichroteph/YACP/issues/13)** proposed the 240-word dictionary limit and originally reported the [updater issue](https://github.com/Sichroteph/YACP/issues/7).
- **[Doxxxxxx](https://github.com/Sichroteph/YACP/issues/7#issuecomment-5866109455)** supplied X3 serial logs showing that beta 1 could fail during the update check, before any download began.
- **[TheRealGieve](https://github.com/Sichroteph/YACP/issues/11)** identified the 4 h 15 Reading Rhythm cap.
- **[ludovic-marce](https://github.com/Sichroteph/YACP/issues/10#issuecomment-5833737447)** suggested exposing Smart Sync in the web portal and validated the beta 1 behavior on an X3.
- **CrossInk and CrossPoint contributors** provided the open-source foundation on which YACP builds. The staged updater also adapts CrossInk's download-to-SD approach.

## Download and install

This is a prerelease for X3 and X4. **Standard**, built as `1.8.1-yacp.8-tiny`, is recommended for most readers and contains built-in 10, 12, 14, and 16 pt fonts. **XLarge**, built separately as `1.8.1-yacp.9-xlarge`, contains built-in 16, 18, and 20 pt fonts. The two variants use consecutive embedded build numbers because every compiled image gets a unique version. Use the matching firmware asset and follow the [installation guide](https://github.com/Sichroteph/YACP/blob/main/docs/installation.md). Keep a copy of the firmware you currently use in case you need to return to it.
