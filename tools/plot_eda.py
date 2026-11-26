#!/usr/bin/env python3
"""Plot an EDA capture and estimate the trigger threshold.

Input: a CSV from capture.py, or a copy of the sketch's serial output.
  odax_atomizer.ino log:  ms,counts,mv_est,sig_fast,sig_base,phasic,atomizer,state,status
  odax_probe.ino capture: ms,counts   (phasic is recomputed with the sketch's filters)

Examples:
  python3 plot_eda.py run.csv
  python3 plot_eda.py probe.csv --polarity -1 --rest 30 210 --stim 240 270 300 330 360
  python3 plot_eda.py run.csv --threshold 8 --show

Threshold estimate (rules of thumb, not validated standards):
  N = spread (1st-99th percentile) of the resting line around its 1 s average, counts
  D = drift of the resting line, counts per minute (absolute)
  S = largest rise of `phasic` in the rest window
  E = rise of `phasic` after each stimulus (peak in the 15 s after, minus the median of the 5 s before)
  T = max(3N, 2D, 1.5S); usable if T <= 0.6 * smallest E
Times are seconds from the start of the log (the x axis of the plot).
"""
import argparse
import math
import os
import sys

import numpy as np
import pandas as pd

FAST_TAU_S = 1.0        # same as FAST_TAU_S in odax_atomizer.ino
BASELINE_TAU_S = 60.0   # same as BASELINE_TAU_S
CALIBRATION_S = 30.0    # same as CALIBRATION_S
STATE_NAMES = {0: "no contact", 1: "calibrating", 2: "armed", 3: "mist", 4: "cooldown"}


def load(path):
    """Read a capture robustly: skip junk before the header, drop spliced/corrupt rows,
    keep time monotonic, and report gaps (lost data) so they are not drawn as a line."""
    import csv
    with open(path, newline="", errors="replace") as f:
        lines = [ln.strip() for ln in f if ln.strip() and not ln.lstrip().startswith("#")]
    hdr = next((i for i, ln in enumerate(lines) if ln.startswith("ms,")), None)
    if hdr is None:
        sys.exit("No header line starting with 'ms,' found.")
    cols = lines[hdr].split(",")
    rows, dropped = [], 0
    last_ms = -1
    for ln in lines[hdr + 1:]:
        parts = ln.split(",")
        if len(parts) != len(cols):
            dropped += 1
            continue
        try:
            vals = [float(p) for p in parts]
        except ValueError:
            dropped += 1
            continue
        d = dict(zip(cols, vals))
        if "state" in d and d["state"] not in (0, 1, 2, 3, 4):
            dropped += 1
            continue
        if "status" in d and d["status"] not in (0, 1):
            dropped += 1
            continue
        if d["ms"] < last_ms:          # time went backwards: leftover from before a reset
            dropped += 1
            continue
        last_ms = d["ms"]
        rows.append(d)
    df = pd.DataFrame(rows)
    if df.empty:
        sys.exit("No data rows found.")
    df["t"] = (df["ms"] - df["ms"].iloc[0]) / 1000.0
    step = float(df["t"].diff().median())
    gaps = df.index[df["t"].diff() > max(1.0, 8 * step)].tolist()
    df.attrs["gaps"] = [(float(df["t"].iloc[i - 1]), float(df["t"].iloc[i])) for i in gaps]
    df.attrs["dropped"] = dropped
    return df


def recompute(df, polarity):
    """Rebuild sig_fast, sig_base, phasic from counts, mirroring the sketch."""
    t = df["t"].to_numpy()
    sig = polarity * df["counts"].to_numpy()
    fast = np.full(len(t), np.nan)
    base = np.full(len(t), np.nan)
    f = sig[0]
    b = None
    cal_end = t[0] + CALIBRATION_S
    cal_vals = []
    for i in range(len(t)):
        dt_ = t[i] - t[i - 1] if i else 0.0
        f = f + (1 - math.exp(-dt_ / FAST_TAU_S)) * (sig[i] - f) if i else sig[i]
        fast[i] = f
        if b is None:
            cal_vals.append(sig[i])
            if t[i] >= cal_end:
                b = float(f)   # new sketch: baseline starts at the current fast level
                base[i] = b
        else:
            b = b + (1 - math.exp(-dt_ / BASELINE_TAU_S)) * (sig[i] - b)
            base[i] = b
    df["sig_fast"], df["sig_base"] = fast, base
    df["phasic"] = fast - base
    return df


def window(df, a, b):
    return df[(df["t"] >= a) & (df["t"] <= b)]


def estimate(df, rest, stims, polarity):
    out = {}
    w = window(df, *rest) if rest else df
    if len(w) < 20:
        return {"error": "rest window has too few samples"}
    t, c = w["t"].to_numpy(), polarity * w["counts"].to_numpy()
    dt_med = float(np.median(np.diff(t))) if len(t) > 1 else 0.05
    k = max(3, int(round(1.0 / dt_med)))
    smooth = pd.Series(c).rolling(k, center=True, min_periods=1).mean().to_numpy()
    resid = c - smooth
    lo, hi = np.percentile(resid, [1, 99])
    out["N"] = float(hi - lo)
    out["D"] = float(abs(np.polyfit(t, smooth, 1)[0]) * 60.0)
    ph = w["phasic"].dropna()
    out["S"] = float(ph.max()) if len(ph) else float("nan")
    out["rest_note"] = "" if rest else "no --rest given: used the whole log, so S and D include any responses/transients in it"
    es = []
    for s in stims or []:
        pre = df[(df["t"] >= s - 5) & (df["t"] < s)]["phasic"].dropna()
        post = df[(df["t"] >= s) & (df["t"] <= s + 15)]["phasic"].dropna()
        if len(pre) and len(post):
            es.append(float(post.max() - pre.median()))
        else:
            es.append(float("nan"))
    out["E"] = es
    parts = [3 * out["N"], 2 * out["D"]]
    if not math.isnan(out["S"]):
        parts.append(1.5 * out["S"])
    out["T"] = max(parts)
    good = [e for e in es if not math.isnan(e)]
    out["Emin"] = min(good) if good else float("nan")
    out["ok"] = (out["T"] <= 0.6 * out["Emin"]) if good else None
    return out


def report(est):
    if "error" in est:
        return est["error"]
    L = ["Threshold estimate (counts)"]
    L.append(f"  N noise (1-99% spread)  {est['N']:.2f}")
    L.append(f"  D drift per minute      {est['D']:.2f}")
    L.append(f"  S largest rest rise     {est['S']:.2f}")
    if est["E"]:
        L.append("  E evoked rises          " + ", ".join(f"{e:.1f}" for e in est["E"]))
    L.append(f"  T = max(3N, 2D, 1.5S)   {est['T']:.2f}")
    if est["ok"] is None:
        L.append("  Add --stim times to check T <= 0.6 * smallest E")
    elif est["ok"]:
        L.append(f"  Check passed: T <= 0.6*Emin ({0.6 * est['Emin']:.2f}). Suggested TRIGGER_RISE_COUNTS ~ {est['T']:.0f}")
    else:
        L.append(f"  Check FAILED: T > 0.6*Emin ({0.6 * est['Emin']:.2f}). Stimulus and spontaneous activity overlap here;"
                 " improve contact, use a stronger stimulus, or accept some spontaneous mist.")
    if est["rest_note"]:
        L.append("  Note: " + est["rest_note"])
    L.append("  (multipliers are rules of thumb, not validated standards)")
    return "\n".join(L)


def shade(ax, t, mask, color, alpha):
    """Shade contiguous True runs of mask."""
    start = None
    for i in range(len(t)):
        if mask[i] and start is None:
            start = t[i]
        if (not mask[i] or i == len(t) - 1) and start is not None:
            ax.axvspan(start, t[i], color=color, alpha=alpha, lw=0)
            start = None


def plot(df, args, est, out_png):
    import matplotlib
    if not args.show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    has_state = "state" in df.columns and df["state"].notna().any()
    n = 4 if has_state else 3
    fig, axs = plt.subplots(n, 1, figsize=(11, 2.6 * n + 1), sharex=True,
                            gridspec_kw={"height_ratios": [2, 2, 2] + ([1] if has_state else [])})
    gaps = df.attrs.get("gaps", [])
    if gaps:
        extra = pd.DataFrame({"t": [g[0] + 1e-3 for g in gaps]})
        df = pd.concat([df, extra]).sort_values("t", kind="stable").reset_index(drop=True)
    t = df["t"].to_numpy()
    on = (df["atomizer"].fillna(0).to_numpy() > 0) if "atomizer" in df.columns else np.zeros(len(t), bool)

    axs[0].plot(t, df["counts"], lw=0.8, color="#4c78a8")
    axs[0].set_ylabel("raw counts\n(0-1023)")
    axs[0].set_title(os.path.basename(args.csv))

    axs[1].plot(t, df["sig_fast"], lw=1.0, color="#e45756", label="fast (1 s)")
    axs[1].plot(t, df["sig_base"], lw=1.2, color="#222222", label="baseline (60 s)")
    axs[1].set_ylabel(f"polarity x counts\n(polarity {args.polarity:+g})")
    axs[1].legend(loc="upper right", fontsize=8, frameon=False)

    axs[2].plot(t, df["phasic"], lw=1.0, color="#54a24b")
    axs[2].axhline(0, color="#999999", lw=0.5)
    if args.threshold is not None:
        axs[2].axhline(args.threshold, color="#f58518", ls="--", lw=1.2, label=f"trigger {args.threshold:g}")
        axs[2].legend(loc="upper right", fontsize=8, frameon=False)
    axs[2].set_ylabel("phasic = fast - baseline\n(counts)")

    for ax in axs[:3]:
        for g in gaps:
            ax.axvspan(g[0], g[1], color="#e45756", alpha=0.12, lw=0)
        if on.any():
            shade(ax, t, on, "#72b7b2", 0.35)
        if args.rest:
            ax.axvspan(args.rest[0], args.rest[1], color="#bab0ac", alpha=0.18, lw=0)
        for s in args.stim or []:
            ax.axvline(s, color="#b279a2", ls=":", lw=1)

    if has_state:
        st = df["state"].fillna(0).to_numpy()
        axs[3].step(t, st, where="post", color="#444444", lw=1)
        axs[3].set_yticks(list(STATE_NAMES))
        axs[3].set_yticklabels([STATE_NAMES[k] for k in STATE_NAMES], fontsize=7)
        axs[3].set_ylim(-0.5, 4.5)
        axs[3].set_ylabel("state")
    axs[-1].set_xlabel("time (s)")

    note = ["teal = atomizer on" if on.any() else None,
            "red = lost data" if gaps else None,
            "grey = rest window" if args.rest else None,
            "dotted = stimulus" if args.stim else None]
    note = ", ".join(x for x in note if x)
    if note:
        fig.text(0.01, 0.005, note, fontsize=8, color="#555555")
    fig.tight_layout(rect=(0, 0.02, 1, 1))
    fig.savefig(out_png, dpi=150)
    if args.show:
        plt.show()
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--polarity", type=float, default=None,
                    help="+1 if arousal raises counts, -1 if it lowers them (default 1; ignored when the log already has phasic)")
    ap.add_argument("--rest", type=float, nargs=2, metavar=("START", "END"), help="seconds of quiet sitting still")
    ap.add_argument("--stim", type=float, nargs="*", help="seconds at which you took a deep breath / clapped")
    ap.add_argument("--start", type=float, default=0.0, help="ignore data before this time (s), e.g. while electrodes settle")
    ap.add_argument("--recompute", action="store_true", help="rebuild fast/baseline/phasic from counts even if the log has them")
    ap.add_argument("--threshold", type=float, help="draw this TRIGGER_RISE_COUNTS line")
    ap.add_argument("--out", help="output PNG (default: <csv>.png)")
    ap.add_argument("--show", action="store_true", help="open an interactive window")
    args = ap.parse_args()

    df = load(args.csv)
    if df.attrs["dropped"]:
        print(f"Dropped {df.attrs['dropped']} corrupt/out-of-order rows.")
    for g in df.attrs["gaps"]:
        print(f"WARNING: data missing from {g[0]:.1f} s to {g[1]:.1f} s ({g[1]-g[0]:.0f} s lost in capture).")
    if args.start > 0:
        df = df[df["t"] >= args.start].reset_index(drop=True)
    logged = (not args.recompute) and all(c in df.columns for c in ("sig_fast", "sig_base", "phasic")) and df["phasic"].notna().any()
    if args.polarity is None:
        args.polarity = 1.0
    if not logged:
        df = recompute(df, args.polarity)
        print(f"Recomputed phasic from counts with polarity {args.polarity:+g} "
              f"(baseline starts after {CALIBRATION_S:g} s).")
    else:
        print("Using phasic from the sketch's log (polarity as set in the sketch).")

    est = estimate(df, args.rest, args.stim, args.polarity)
    print(report(est))
    out_png = args.out or os.path.splitext(args.csv)[0] + ".png"
    plot(df, args, est, out_png)
    print(f"Wrote {out_png}")


if __name__ == "__main__":
    main()
