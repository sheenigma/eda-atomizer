# Changelog

## Atomizer still ran with idle HIGH
- With `ACTIVE_HIGH = false` (D7 HIGH = off) the module still ran, so my active-LOW guess was wrong or incomplete. Added `firmware/atomizer_probe/` to find the real behaviour (level, edge toggle, or S not controlling it). `ACTIVE_HIGH` stays provisional.
- With the atomizer wired in, air read 1.0 counts (0.0 with it unplugged), so `NO_CONTACT_BELOW = 1` wrongly saw contact and started calibrating on an open electrode. Now 2 (skin was 2.7 or more). Suggests the atomizer shifts the shared ground or 5V; recheck while it is spraying.

## Atomizer polarity
- Atomizer ran continuously with idle LOW and S confirmed on D7, so it is treated as active-LOW: `ACTIVE_HIGH = false` in `odax_atomizer.ino` and `atomizer.ino`. To be confirmed on hardware with `f`/`a`/`t`. Simulation: D7 idles HIGH, goes LOW at the same burst times as before.

## Real data, 2026-11-25 (ground wire seated, electrodes on hand)
- Air reads 0.0 counts; skin reads 3-17 (resting 6-12, about 1% of the ADC range). Contact raises counts, so `POLARITY = +1`; `NO_CONTACT_BELOW = 1`.
- Counts keep rising for about 40 s after contact, so calibration starting 2 s after contact biased the baseline low (phasic sat at +3 to +5 for minutes). Fix: `CALIBRATION_S = 60` and the baseline now starts at the current fast level instead of the window mean.
- Rest: noise 0.3-1.1 counts p-p, wander about 2 counts per minute, largest rest wiggle about 2 counts. One clean response: +6.3 counts. `TRIGGER_RISE_COUNTS = 4.0` is provisional; the rule of thumb wanted about 4.7, so the margin is thin.
- Earlier log (ground wire unplugged for the first 533 s) is not usable.
- Capture lost data twice (448 s, then 72 s). `capture.py` now echoes every 20th line only and prints a loud warning on any gap; `plot_eda.py` marks gaps in red and ignores junk/corrupt rows. Cause not pinned down: suspect the Mac throttling Terminal while it is in the background.
- Simulation regression after the sketch changes: bursts at 71.9 s and 116.7 s, test burst 200 s, safety cutoff 235 s, same for both polarities.

## Tools
- Added `tools/capture.py` (serial to CSV) and `tools/plot_eda.py` (plots plus N/D/S/E threshold estimate). Tested on synthetic logs and the simulated sketch log: recovered noise, drift and evoked rise sizes within the expected smoothing loss, same result for polarity +1 and -1.

## ODAX sketch
- Trigger works on `phasic = fast − baseline` and needs no µS conversion.
- Added re-arm-needs-relax logic, found in simulation: the same excursion re-triggered right after cooldown.
- Replaced `FAST_ALPHA = 0.3` with `FAST_TAU_S = 1.0`. The old constant was commented as about 1 s but was about 0.1 s.

## Poh front-end sketches
- Conductance equation derived and checked against the paper's plots; sketches superseded by the ODAX board.
