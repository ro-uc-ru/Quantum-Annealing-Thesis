#!/usr/bin/env python3
"""Infidelity vs t/T per schedule, like Fig. 3 of docs/TFG_UCEDA_RUIZ_ROGER.pdf.

Usage: scripts/plot-infidelity.py [--log] [--xmin=<x>] <out.png> <run-dir> [<run-dir> ...]
Each run dir holds config.json and snapshots/snap_<j>.csv (columns k,p).
Infidelity = 1 - P_gs, with P_gs the probability on the ground set of the 3x3
target Hamiltonian (the 8 degenerate solutions of the TFG, Sec. IV C).
"""
import csv, glob, json, os, re, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

GROUND = {3: [12, 33, 66, 96, 129, 132, 258, 264]}

def series(d):
    cfg = json.load(open(os.path.join(d, "config.json")))
    gs = GROUND[cfg["N"]]
    pts = []
    for f in glob.glob(os.path.join(d, "snapshots", "snap_*.csv")):
        j = int(re.search(r"snap_(\d+)", f).group(1))
        p = [float(r["p"]) for r in csv.DictReader(open(f))]
        pts.append((j / cfg["steps"], 1.0 - sum(p[k] for k in gs)))
    return cfg, sorted(pts)

args = sys.argv[1:]
log = "--log" in args
xmin = next((float(a.split("=")[1]) for a in args if a.startswith("--xmin=")), 0.0)
args = [a for a in args if not a.startswith("--")]
out, dirs = args[0], args[1:]
FLOOR = 1e-16  # log axis cannot show 0; values below are clipped here
fig, ax = plt.subplots(figsize=(7, 4.8))
runs = [series(d) for d in dirs]
same = len({c["family"] for c, _ in runs}) == 1  # one family: label by T
for cfg, pts in runs:
    pts = [(x, max(y, FLOOR)) for x, y in pts] if log else pts
    pts = [(x, y) for x, y in pts if x >= xmin]  # crop the x axis
    ax.plot(*zip(*pts), marker="o", ms=3, lw=1.2, label=f"$\\tau$ = {cfg['T']:g} $t_0$" if same else cfg["family"])
ax.set_xlabel(r"$t/\tau$")
ax.set_ylabel("infidelity  $1 - P_{gs}$")
ax.set_title(f"{cfg['N']}x{cfg['N']}, {cfg['family']}, steps={cfg['steps']}" if same
             else f"{cfg['N']}x{cfg['N']}, $\\tau$={cfg['T']:g} $t_0$, steps={cfg['steps']}")
if log:
    ax.set_yscale("log")
ax.set_xlim(xmin, 1.0)
ax.grid(alpha=0.3, which="both")
ax.legend()
fig.tight_layout()
fig.savefig(out, dpi=150)
