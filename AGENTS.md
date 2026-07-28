# CrossInk — Shared Agent Guide

This is the canonical repo instruction file.
`CLAUDE.md` should point here so Codex and Claude read the same guidance.

Project: Open-source e-reader firmware for Xteink X4 (ESP32-C3).

## Core Rules

- Role: Senior Embedded Systems Engineer for ESP-IDF / Arduino-ESP32 work.
- The ESP32-C3 has no PSRAM and about 380 KB usable RAM. Stability beats features.
- Cite file paths and line numbers before proposing non-trivial changes.
- Do not assume ESP-IDF or SDK API availability. Verify in `open-x4-sdk/` or the live code.
- Do not claim performance or memory wins without explaining the mechanism, such as reduced heap churn, flash vs DRAM placement, or smaller stack use.
- Justify new heap allocations or explain why stack/static storage is not suitable.
- Explain fixes in plain language where possible, ideally in terms a Node / React developer would follow.
- After proposing or making a fix, say how to verify it on hardware.

## Persistent Context

- Read `.claude/CONTEXT.md` at session start for durable repo-specific gotchas.
- Keep `.claude/CONTEXT.md` short. Add only reusable findings, not turn-by-turn history.
- If asked to summarize a session, create `.claude/CONTEXT-YYYY-MM-DD.md` with the relevant findings for that session.

## Repo Skills

- Do not read every `.claude/skills/*/SKILL.md` at session start.
- Use this section as an index. Read a local skill only when the task clearly matches its folder name or purpose.
- Current local skills:
  - `control-flow-clarity`: simplify confusing logic without behavior changes.
  - `refactor-for-review`: small refactors intended to reduce review risk.
  - `hal-and-abstractions`: HAL boundaries and platform abstraction work.
  - `heap-discipline`: memory allocation, lifetime, and fragmentation-sensitive work.
  - `scope-discipline`: keep changes narrow and avoid unrelated cleanup.
  - `custom-fonts`: font generation, conversion, and SD/built-in font work.
- Treat these as task-specific playbooks layered on top of this guide. If a skill conflicts with this file, prefer `AGENTS.md` and note the conflict.

## Hardware Constraints

- MCU: ESP32-C3, single-core RISC-V at 160 MHz.
- Display: 800x480 e-ink.
- Single framebuffer only: `800 * 480 / 8 = 48000` bytes.
- Storage is SD via SdFat. On real hardware, only one reader can hold a file open at a time.

## Resource Rules

1. Keep local stack usage small. Anything meaningfully larger than 256 bytes should be justified.
2. Avoid repeated heap churn in loops. Allocate once in `onEnter()`, reuse, and free in `onExit()`.
3. Large constant tables should be `static const` so they live in flash, not DRAM.
4. Avoid `std::string` and Arduino `String` in hot paths. Prefer `string_view`, `char[]`, and `snprintf`.
5. All user-facing UI strings must use `tr(STR_*)`. Logs may be hardcoded.
6. Prefer `constexpr` for compile-time constants.
7. Reserve `std::vector` capacity before push loops.
8. Debounce persistent writes. Do not write progress on every page turn.
9. `new` is not nothrow on ESP32. With exceptions disabled, bare `new` calls `abort()` on allocation failure instead of returning `nullptr`. Use `new (std::nothrow)` or `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h` for fallible allocations.
10. Prefer `makeUniqueNoThrow<T>()` / `makeUniqueNoThrow<T[]>()` for owned heap allocations so cleanup is automatic on early returns.
11. Use raw `malloc` or `new (std::nothrow)` only when a C or SDK API takes ownership; add a short comment explaining that ownership transfer.

## HAL And Platform Rules

- Use HAL classes, not SDK classes, in app code.
- File I/O uses `FsFile`, not Arduino `File`.
- Always close files explicitly.
- Use `MappedInputManager::Button::*` enums for button logic.

## C++ / Embedded Gotchas

- `string_view::data()` is not null-terminated. Do not pass it directly to C APIs.
- ISR handlers need `IRAM_ATTR`, and ISR-read data must be in DRAM, not flash-only storage.
- Never call `xSemaphoreTake()` from an ISR. Use ISR-safe give APIs.
- Do not cast unaligned `uint8_t*` data to wider pointer types. Use `memcpy`.
- No exceptions. No `abort()`. Log before returning failure.
- Avoid `std::function` in hot paths and library code; prefer function pointers or a small context/callback struct.
- Keep template use deliberate. If a template is needed in shared code, consider explicit instantiation in a `.cpp` file to avoid repeated binary growth.

## Error Handling

- Prefer `LOG_ERR(...)` plus `return false` for recoverable failures.
- Prefer `LOG_ERR(...)` plus a known fallback when the app can continue safely.
- Use `assert(false)` only for truly impossible fatal states.
- Use `ESP.restart()` only for intentional recovery flows, such as completing OTA.
- Always log before returning failure from allocation, file, parse, network, or hardware paths.

## Activity Lifecycle

- Activities are heap-allocated and deleted on exit.
- Allocate long-lived buffers and tasks in `onEnter()`.
- Free resources in reverse order in `onExit()`.
- Delete FreeRTOS tasks before the activity is destroyed.
- Close open file handles in `onExit()`.
- Typical task stacks:
  - 2048 bytes for simple rendering work
  - 4096 bytes for network or EPUB parsing work

## UI And Input

- Do not hardcode screen dimensions like `800` or `480`; use renderer dimensions and orientation helpers.
- Use `renderer.getOrientedViewableTRBL()` for layout that must stay inside usable bezel-safe bounds.
- Use logical `MappedInputManager::Button::*` values in activities; raw hardware button indices belong only in button-mapping code.
- Route UI drawing through `UITheme` / `GUI` where practical so fonts, spacing, and orientation behavior stay consistent.
- User-facing text must be translated with `tr(STR_*)`; logs can remain hardcoded.

## Build And Verification

- PlatformIO is the source of truth. Personal overrides belong in `platformio.local.ini`.
- Host environment may be macOS, Linux, WSL, or Windows Git Bash. Check `uname -s` before recommending platform-specific shell commands.
- Logging uses `LOG_INF`, `LOG_DBG`, and `LOG_ERR`.
- The simulator env in this repo is `simulator`.
- For simulator work, build from this firmware repo unless the change belongs in `crosspoint-simulator` itself.
- Every modification to this YACP firmware must be validated with `pio run -e tiny`. Builds of other environments
  may be additional, but they do not replace the required Tiny build.
- Before every YACP firmware compilation, including a retry after a failed build, increment
  `crosspoint.crossink_version` in `platformio.ini`. Never compile or transfer two different YACP firmware images
  with the same embedded version.
- Common validation commands:
  - `pio run -e simulator` for simulator-facing UI/reader work.
  - `pio run -e default` for firmware compile validation.
  - `pio check -e default --fail-on-defect low --fail-on-defect medium --fail-on-defect high` for static analysis.
  - `find src lib include test -name "*.cpp" -o -name "*.h" | xargs clang-format -i` for formatting touched C++ files.
- For crash debugging, check serial logs, heap with `ESP.getFreeHeap()`, task stack high-water marks, and whether cache files need clearing.
- Hardware verification should mention the concrete device path to test, expected UI/log behavior, and any cache reset needed.

## Firmware Artifact Naming

- Treat PlatformIO's `firmware.bin` and `firmware-<environment>.bin` as internal build artifacts, not as names for
  user-facing YACP deliverables.
- Name a YACP deliverable `YACP-<crossink_version>-<variant>.bin`, using the `crossink_version` value from
  `platformio.ini`. For example: `YACP-1.4.0-yacp-tiny.bin` and `YACP-1.4.0-yacp-xlarge.bin`.
- Before copying a firmware to a device, verify its embedded YACP version and variant. Do not infer the variant only
  from a `default` build-directory name.
- Keep the descriptive YACP filename on the device. Use the exact name `firmware.bin` only when a specific flashing
  mechanism technically requires it, and then explain that temporary compatibility requirement to the user.

## Generated Files

- Do not edit generated files directly.
- Web portal headers under `src/network/html/*.generated.h` are built by `scripts/build_web.py` from sources in `web/`: pages compose `web/templates/base.html` (shared chrome) with `web/pages/<slug>.{html,css,js}`, plus shared assets `web/assets/style.css` (served at `/style.css`) and `web/assets/logo.png` (served at `/logo.png`). Edit the `web/` sources, never the generated headers.
- I18n generated files under `lib/I18n/` come from `lib/I18n/translations/*.yaml` via `scripts/gen_i18n.py`.

## Cache Format

- EPUB cache lives under `.crosspoint/epub_<hash>/`.
- If you change binary cache layouts, bump the format version first and document it in `docs/file-formats.md`.
- Cache identity is tied to the book path hash; moving or renaming a book creates a different cache.
- Clear the relevant `.crosspoint/epub_<hash>/` cache when testing EPUB parser, layout, image, or binary cache format changes that may otherwise reuse stale output.

## Git Workflow

- Check `git status --short` before edits and before reporting results. Preserve unrelated user changes.
- Do not commit unless the user explicitly asks.
- Before staging, ensure ignored/generated/local files such as `.pio/`, `*.generated.h`, `compile_commands.json`, and `platformio.local.ini` are not included.
- Branch names should use repo-style prefixes such as `feat/`, `fix/`, `docs/`, `refactor/`, `test/`, or `chore/`.
- Suggested commit messages should follow `<type>: <short summary>`, using types like `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, or `perf`.

## Changelog

When new features are added or issues are fixed, make sure to add an entry to `CHANGELOG.md` with the user-facing description of the change. Types of changes should have their own section.

### Changelog Guiding Principles

- Changelogs are _for humans_, not machines.
- There should be an entry for every single version.
- The same types of changes should be grouped.
- Versions and sections should be linkable.
- The latest version comes first.
- The release date of each version is displayed.

### Types of Changelog Changes
- Added - for new features.
- Changed - for changes in existing functionality.
- Deprecated - for soon-to-be removed features.
- Removed - for now removed features.
- Fixed - for any bug fixes.
- Security - in case of vulnerabilities.
