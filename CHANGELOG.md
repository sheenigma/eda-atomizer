# Changelog

## ODAX sketch
- Trigger works on `phasic = fast − baseline` and needs no µS conversion.
- Added re-arm-needs-relax logic, found in simulation: the same excursion re-triggered right after cooldown.
- Replaced `FAST_ALPHA = 0.3` with `FAST_TAU_S = 1.0`. The old constant was commented as about 1 s but was about 0.1 s.

## Poh front-end sketches
- Conductance equation derived and checked against the paper's plots; sketches superseded by the ODAX board.
