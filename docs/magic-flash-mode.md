# X3 display modes

Settings > Display > Screen refresh offers an X3-only Display mode picker:

| Mode | Text-page cleanup | Text smoothing |
| --- | --- | --- |
| Magic Flash | Two no-flash passes on each ordinary text page | Off, locked |
| Classic (CrossPoint) | Full cleanup at the selected interval | User's choice |

Magic Flash is the default when an X3 has no saved mode. An explicitly saved Classic choice is retained. Both earlier reinforcement values (1 and 2) now select the two-pass Magic Flash behavior. The picker presents only Magic Flash and Classic, with a description for each choice.

In Magic Flash, the one-page interval and disabled text smoothing remain visible as locked, shaded rows. Text Settings > Style also explains that smoothing is unavailable with Magic Flash. Returning to Classic restores its preceding settings.

## Persistence and reader safety

The existing `refreshAction` storage values remain compatible: 0 is Classic, 1 is a legacy alias for Magic Flash, and 2 is Magic Flash. A new mode choice writes 0 or 2. The raw `refreshFrequency` and `textAntiAliasing` fields preserve the Classic preferences. Effective getters return one page and no text smoothing in Magic Flash. This also prevents existing per-book smoothing preferences from bypassing the mode, without rewriting or erasing those preferences. No binary book-profile layout changes or cache reset are needed.

Readers consult the effective values. A stale Classic countdown cannot postpone Magic Flash's next scheduled reinforcement. Required cleanup takes priority, images retain their own grayscale path, and manual actions retain their existing behavior. No new framebuffer is allocated. The two passes reuse the existing BW buffer.

The web settings expose `displayMode` (0 Magic Flash, 1 Classic) only on X3, plus the effective values, lock explanation and preserved `classicValue` for cadence and smoothing. Resolve the requested mode before other fields in the same POST. Ignore changes to locked fields while Magic Flash is selected. The legacy `refreshAction` POST is still accepted on X3, but is hidden from the picker API. Non-X3 readers neither expose nor apply Magic Flash, even with a settings file copied from X3.

## Verification

- `verify_yacp_display_modes.py`: default X3 mode, picker confirmation and cancellation, locked controls, Classic preference restoration, persistence after restart and HTTP batch updates. On X4 Pro, assert no Magic Flash picker and no enforced cadence or smoothing.
- `verify_yacp_refresh.py`: EPUB and TXT with both stored smoothing choices and all three storage IDs. On X3, legacy IDs 1 and 2 must produce two passes with no gray text pass at every ordinary text-page turn, even with a stored 30-page cadence. Compare their simulated pages with Classic without smoothing. On X4 Pro, neither ID may schedule reinforcement or force smoothing off.
- Inspect English simulator captures for the picker descriptions, shaded locked rows and the Text Settings explanation.
- On physical X3, switch modes, reopen a book with a saved smoothing preference and confirm the locked values. Compare ordinary text page turns, image pages and explicit refresh actions. Then return to Classic and verify its prior cadence and smoothing return. Hardware waveform, contrast and battery effects remain unmeasured by simulator tests.
