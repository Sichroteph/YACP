# YACP scope

YACP is a personal, opinionated firmware for Xteink X3 and X4 readers. The X3 is the primary optimization target.

## Core purpose

The firmware exists to read books while using as little energy and unnecessary work as practical. The priorities are:

1. Reduce energy used while a book is open and waiting for input.
2. Reduce CPU, display, SD-card, allocation, and network work.
3. Complete page turns, chapter changes, resume, and rendering quickly.
4. Stay reliable on a single-core ESP32-C3 with no PSRAM.
5. Retain a small set of personally useful features, especially reading statistics.

## Usage assumptions

- A YACP reader normally keeps one book open and reads it sequentially until it is finished.
- Quick Resume after an automatic timeout is the normal resume path. Building the next sequential chapter near the
  end of the current one is expected work, provided the existing memory guard accepts it.
- Lexend Deca and Bitter are the normal font path. SD-card fonts remain available for comfort, but must add no SD
  scan, catalogue RAM, font-file access, or cache cost until explicitly selected or managed.
- Settings are changed rarely, so their write frequency is not an optimization target without contrary measurements.
- Extra themes may remain when inactive themes have no runtime cost. Coherence is required from the default theme,
  YACP: a rounded, cover-free Home centered on resuming the current book. It shows only progress, reading time, and
  estimated time left; its lateral actions browse books in most-recent-first opening order while retaining only the
  selected book on Home. Detailed statistics and secondary services remain available on demand.
- Home must not deserialize or rebuild the current reading page solely for decoration. Real page text belongs to the
  reader path unless a future design can reuse already-available data without an extra SD read, cache, or large
  allocation.

## In scope

- Idle power behavior that remains reliable on the real board.
- Rendering, EPUB parsing, cache, and resume improvements.
- Fewer persistent writes and less SD-card activity.
- Lower allocation churn and smaller memory peaks.
- Display refresh changes that reduce work without hiding required synchronization.
- Reading statistics that do not compromise reader stability.
- Selective CrossPoint or CrossInk updates that support these goals.

## Normally out of scope

- General feature growth.
- Background network services.
- Interactive applications unrelated to reading.
- Media playback.
- Automatic parity with CrossPoint or CrossInk.
- Work whose main purpose is serving a broader set of user preferences.

An exception can be kept when it is personally useful and its cost is understood. Reading Rhythm is the current
example.

## Upstream policy

YACP starts from CrossInk 1.4.0, which is based on CrossPoint Reader. Upstream changes are reviewed when they reduce
energy use, reduce work, improve reliability, or finish an existing operation faster. A newer upstream implementation
is not adopted only because it is newer.

## Participation

This scope is not a request for proposals. Issues and Discussions are disabled, pull requests are closed automatically,
and no project contact channel is provided. Forks are the supported way to pursue a different direction.
