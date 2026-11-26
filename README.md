# EDA → atomizer

Electrodermal activity (skin conductance) triggers an ultrasonic atomizer on an Arduino Uno R3.

## Stages
1. **Stage 1 (current):** ODAX EDA Maker Kit board + ACEBOTT atomizer on the Uno.
2. **Stage 2 (after Stage 1 is confirmed on hardware):** microSD logging (SPI D10–D13, set `MIRROR_LED 0`) and a battery pack.

## Wiring (Stage 1)
| From | To |
|---|---|
| ODAX VCC | 5V |
| ODAX GND | GND |
| ODAX OUT | A0 |
| Atomizer V | 5V |
| Atomizer G | GND |
| Atomizer S | D7 |

The Uno has one 5V pin, so share it through one breadboard strip row. Never feed 5 V into Vin.

## Layout
- `firmware/odax_probe/` — streams raw counts to the Serial Plotter. Use it to find polarity, noise, drift and response size.
- `firmware/odax_atomizer/` — current sketch: EDA-triggered mist with calibration, cooldown, re-arm and a safety cutoff. Serial commands: `a` auto, `o` on, `f` off, `t` test burst, `c` recalibrate, `?` help.
- `firmware/atomizer/` — standalone atomizer test.
- `firmware/eda_logger/`, `firmware/eda_atomizer/`, `hardware/eda_frontend.cir` — earlier Poh et al. 2010 op-amp front end (superseded by the ODAX board).
- `tools/` — Python capture and plotting (see below).
- `sim/` — host-side C++ harnesses with a stub `Arduino.h` and simulated signals.

## Atomizer check (do this before any real run)
The module appears to be **active-LOW** (it ran continuously when D7 idled LOW), so the sketches use `ACTIVE_HIGH = false`: D7 idles HIGH and goes LOW to spray. Unconfirmed until tested. With S on D7, V on 5V, G on GND: send `f` (must be off), `a` (stays off), `t` (mists about 5 s, then stops). Mists on `f` or idle: set `ACTIVE_HIGH = true`. Toggles on each command instead of timing out: it is a falling-edge toggle and the sketch needs a pulse mode.
D7 floats during reset and upload (about 1-2 s), so the atomizer may spray briefly each time `capture.py` opens the port. A 10 k pull-up from S to 5V holds it off.

## Capture and plot (Python)
```
pip install -r tools/requirements.txt
python3 tools/capture.py --out data/rest_run.csv          # close the Arduino Serial Monitor first
python3 tools/plot_eda.py data/rest_run.csv --polarity 1 --rest 40 220 --stim 240 270 300 330 360 --threshold 8
```
- `capture.py` saves either sketch's output to CSV, echoes only every 20th line, and warns loudly if data is lost (probe `counts:` lines get PC-clock timestamps). Type `a/o/f/t/c` + Return to send commands. Opening the port resets the Uno, so it recalibrates.
- `plot_eda.py` (`--start T` skips the settling period, `--recompute` rebuilds phasic from counts) draws raw counts, fast/baseline, `phasic` with the trigger line and atomizer-on shading, and state. It prints N, D, S, E and a suggested `TRIGGER_RISE_COUNTS`. Times are seconds in the plot (`--rest` is a quiet-sitting window, `--stim` the times of your breaths or claps).
- `tools/examples/` has a synthetic example. Neither tool has been run on real hardware data yet.

## Status and caveats
- Simulated only: not compiled with the AVR toolchain and not run on hardware.
- ODAX output polarity and transfer function (counts → µS) are unknown, so thresholds are in raw counts.
- `TRIGGER_RISE_COUNTS = 6` is a placeholder. Set it from probe data: T = max(3N, 2D, 1.5S), and require T ≤ 0.6·E_min (rules of thumb, not validated standards).
- Atomizer trigger polarity (`ACTIVE_HIGH`) and whether it mists on this wiring are unconfirmed.

## Log
See `CHANGELOG.md`.
