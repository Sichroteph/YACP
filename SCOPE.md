# YACP scope

YACP is a personal, opinionated firmware for Xteink X3 and X4 readers. The X3 is the primary optimization target.

## Core purpose

The firmware exists to read books while using as little energy and unnecessary work as practical. The priorities are:

1. Reduce energy used while a book is open and waiting for input.
2. Reduce CPU, display, SD-card, allocation, and network work.
3. Complete page turns, chapter changes, resume, and rendering quickly.
4. Stay reliable on a single-core ESP32-C3 with no PSRAM.
5. Retain a small set of personally useful features, especially reading statistics.

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
