# YACP roadmap

The roadmap is intentionally short. Work enters this list only when it supports battery life, execution efficiency, rendering reliability, or the small reading-statistics exception.

## Current

- [ ] Measure X3 reader idle current at the normal and 10 MHz CPU frequencies.
- [ ] Measure current while the e-ink BUSY line is asserted at normal and reduced CPU frequency.
- [ ] Verify that the 50 ms X3 button response remains consistent across long reading sessions.
- [ ] Measure the reduction in SD progress writes during a typical session.
- [ ] Validate Quick Resume across light and grayscale EPUB pages on X3.
- [ ] Verify Reading Rhythm rollover at week, month, year, and daylight-saving boundaries.

## Upstream watch

CrossPoint Reader and CrossInk are reviewed selectively. A candidate change should satisfy at least one of these questions:

- Does it reduce awake time?
- Does it reduce CPU, display, SD-card, or network work?
- Does it avoid allocation churn or a low-memory retry?
- Does it complete an existing operation faster?

If the answer is no, it is normally left upstream.

## Not planned

- A general feature request queue.
- A public support channel.
- Community governance.
- Automatic parity with every CrossPoint or CrossInk release.
