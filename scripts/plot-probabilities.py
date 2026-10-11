#!/usr/bin/env python3
"""Probability of measuring each state at t = T, like Fig. 4 of the TFG.

Usage: scripts/plot-probabilities.py <out.png> <run-dir> [<run-dir> ...]
One panel per run dir (config.json + snapshots/snap_<steps>.csv, columns k,p).
x: state index k; y: p_k on a log axis (zeros are clipped to FLOOR). States
with p_k >= 0.01 are highlighted: the ground set, since this is H_target.
"""
import csv, json, os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

FLOOR = 1e-16  # log axis cannot show 0
out, dirs = sys.argv[1], sys.argv[2:]
fig, axes = plt.subplots(len(dirs), 1, figsize=(9, 4.2 * len(dirs)), squeeze=False)
for ax, d in zip(axes[:, 0], dirs):
    cfg = json.load(open(os.path.join(d, "config.json")))
    f = os.path.join(d, "snapshots", f"snap_{cfg['steps']:0{len(str(cfg['steps']))}d}.csv")
    p = [float(r["p"]) for r in csv.DictReader(open(f))]
    ks = range(len(p))
    ys = [max(x, FLOOR) for x in p]
    ax.scatter(ks, ys, s=3, color="tab:blue", label="other states")
    hi = [k for k in ks if p[k] >= 0.01]
    ax.scatter(hi, [p[k] for k in hi], s=25, color="tab:red",
               label=f"{len(hi)} ground states (sum = {sum(p[k] for k in hi):.6f})")
    ax.set_yscale("log")
    ax.set_ylim(1e-14, 2)
    ax.set_xlabel("state $k$")
    ax.set_ylabel("$|c_k|^2$")
    ax.set_title(f"{cfg['N']}x{cfg['N']}, {cfg['family']}, $\\tau$={cfg['T']:g} $t_0$, steps={cfg['steps']}")
    ax.grid(alpha=0.3, which="both")
    ax.legend(loc="upper right")
fig.tight_layout()
fig.savefig(out, dpi=150)
