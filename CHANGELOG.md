# Changelog

## Tools
- Added `tools/capture.py` (serial to CSV) and `tools/plot_eda.py` (plots plus N/D/S/E threshold estimate). Tested on synthetic logs and the simulated sketch log: recovered noise, drift and evoked rise sizes within the expected smoothing loss, same result for polarity +1 and -1.

## ODAX sketch
- Trigger works on `phasic = fast − baseline` and needs no µS conversion.
- Added re-arm-needs-relax logic, found in simulation: the same excursion re-triggered right after cooldown.
- Replaced `FAST_ALPHA = 0.3` with `FAST_TAU_S = 1.0`. The old constant was commented as about 1 s but was about 0.1 s.

## Poh front-end sketches
- Conductance equation derived and checked against the paper's plots; sketches superseded by the ODAX board.
