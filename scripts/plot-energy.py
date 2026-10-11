#!/usr/bin/env python3
"""Plot <H(t)> against t from the trace.csv of one or more evolution runs.

Usage: scripts/plot-energy.py <out.png> <run-dir> [<run-dir> ...]
Each run dir holds trace.csv and config.json (spec 005-evolution).
"""
import csv, json, sys, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def load(d):
    with open(os.path.join(d, "trace.csv")) as f:
        rows = list(csv.DictReader(f))
    with open(os.path.join(d, "config.json")) as f:
        cfg = json.load(f)
    return [float(r["t"]) for r in rows], [float(r["energy"]) for r in rows], cfg

out, dirs = sys.argv[1], sys.argv[2:]
fig, axes = plt.subplots(1, len(dirs), figsize=(6 * len(dirs), 4.5), squeeze=False)
for ax, d in zip(axes[0], dirs):
    t, e, c = load(d)
    ax.plot(t, e, lw=1.2)
    ax.set_title(f"{c['N']}x{c['N']}, {c['family']}, T={c['T']:g}, steps={c['steps']}")
    ax.set_xlabel("t")
    ax.set_ylabel(r"$\langle H(t)\rangle$")
    ax.grid(alpha=0.3)
fig.tight_layout()
fig.savefig(out, dpi=150)
