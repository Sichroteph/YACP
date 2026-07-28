# Build checklist

This checklist is printed in the GitHub Actions build summary and attached to each draft release.

## Required for every main build

- [ ] `crosspoint.crossink_version` changed from the previous commit.
- [ ] The top `CHANGELOG.md` version matches the embedded version.
- [ ] User-facing additions, changes, and fixes are described factually.
- [ ] `pio run -e tiny` succeeds.
- [ ] `pio run -e xlarge` succeeds.
- [ ] Firmware files use the `YACP-<version>-yacp-<variant>.bin` naming rule.
- [ ] SHA-256 checksums are generated.

## Required when applicable

- [ ] `pio run -e simulator_x3` succeeds for reader UI or statistics changes.
- [ ] Simulator screenshots use generated data and contain no personal data.
- [ ] X3 hardware behavior is checked on the exact reader path that changed.
- [ ] X4 behavior is checked when code outside an X3 guard changed.
- [ ] Idle current, refresh current, or elapsed time is measured before making a numerical efficiency claim.
- [ ] Relevant EPUB caches are cleared after parser, layout, image, or cache-format changes.

## Release status

A CI success means the source compiled. It does not imply hardware validation. Draft releases must state hardware status before publication.
