#!/usr/bin/env python3
"""Builds the measurement pack embedded in the plugin from a local AutoEq checkout.

    tools/build_measurement_db.py /path/to/AutoEq [output_dir]      (default output: data/measurements)

Writes
  measurements.tsv     one line per entry: source, form, rig, brand, name, model, kind ("" = raw measurement, "eq" = AutoEq result)
  measurements.bin.gz  gzip of N x 480 little-endian int16 (dB * 100) on a log grid, 20 Hz .. 20 kHz, same order

crinacle's raw measurements are not part of the AutoEq checkout, only AutoEq's finished results for them
(results/crinacle/<rig>/<name>/<name> GraphicEQ.txt), so those entries hold the EQ curve itself ("eq") instead of a measurement.

The data keeps its original licences (the AutoEq code is MIT, the measurements belong to their authors), so the output
folder is git-ignored: the plugin builds without it, just without the measurement search.
"""
import bisect, collections, gzip, math, os, re, struct, sys, urllib.parse

POINTS, F0, F1 = 480, 20.0, 20000.0
root = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "measurements")
os.makedirs(out, exist_ok=True)
meas = os.path.join(root, "measurements")

brands = []
for line in open(os.path.join(root, "dbtools", "manufacturers.tsv"), encoding="utf-8"):
    b = line.rstrip("\n").split("\t")[0].strip()
    if b:
        brands.append(b)
brands.sort(key=len, reverse=True)

def split_brand(name):
    low = name.lower()
    for b in brands:
        if low == b.lower() or low.startswith(b.lower() + " "):
            return b, name[len(b):].strip()
    return "", name

rig_of = {}   # rigs that only the results index knows: "- [Name](./Source/<Rig> <form>/Name) by Source on Rig"
for line in open(os.path.join(root, "results", "INDEX.md"), encoding="utf-8"):
    m = re.match(r"- \[(.*?)\]\(\./(.*?)\) by (.*?) on (.*)$", line.strip())
    if m:
        rig_of[(m.group(3), urllib.parse.unquote(m.group(2)).split("/")[-1])] = m.group(4)

def read_curve(path):
    f, g = [], []
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.strip().split(",")
        try:
            x, y = float(p[0]), float(p[1])
        except (ValueError, IndexError):
            continue
        if x > 0 and (not f or x > f[-1]):
            f.append(x); g.append(y)
    return f, g

def resample(f, g):
    res = []
    for i in range(POINTS):
        x = F0 * (F1 / F0) ** (i / (POINTS - 1))
        if x <= f[0]: y = g[0]
        elif x >= f[-1]: y = g[-1]
        else:
            k = bisect.bisect_right(f, x) - 1
            t = math.log(x / f[k]) / math.log(f[k + 1] / f[k])
            y = g[k] + t * (g[k + 1] - g[k])
        res.append(max(-32767, min(32767, round(y * 100))))
    return res

rows, blobs, skipped = [], [], 0
for src in sorted(os.listdir(meas)):
    base = os.path.join(meas, src, "data")
    if not os.path.isdir(base):
        continue
    for dp, _, files in sorted(os.walk(base)):
        for fn in sorted(files):
            if not fn.endswith(".csv"):
                continue
            f, g = read_curve(os.path.join(dp, fn))
            if len(f) < 8:
                skipped += 1; continue
            rel = os.path.relpath(os.path.join(dp, fn), base).split(os.sep)
            name = fn[:-4]
            rig = rel[1] if len(rel) > 2 else rig_of.get((src, name), "")
            brand, model = split_brand(name)
            rows.append((src, rel[0], rig, brand, name, model, ""))
            blobs.append(struct.pack("<%dh" % POINTS, *resample(f, g)))

# crinacle: AutoEq's own EQ curves (GraphicEQ: "f g; f g; ...") for measurements that are not in the checkout
cri = os.path.join(root, "results", "crinacle")
n_eq = 0
if os.path.isdir(cri):
    for rigdir in sorted(os.listdir(cri)):
        full = os.path.join(cri, rigdir)
        m = re.match(r"(.*) (in-ear|over-ear|earbud)$", rigdir)
        if not os.path.isdir(full) or not m:
            continue
        rig, form = m.groups()
        for name in sorted(os.listdir(full)):
            gp = os.path.join(full, name, name + " GraphicEQ.txt")
            if not os.path.isfile(gp):
                continue
            pairs = re.findall(r"([\d.]+) (-?[\d.]+)", open(gp, encoding="utf-8").read())
            f = [float(a) for a, _ in pairs]; g = [float(b) for _, b in pairs]
            if len(f) < 8:
                continue
            brand, model = split_brand(name)
            rows.append(("crinacle", form, rig, brand, name, model, "eq"))
            blobs.append(struct.pack("<%dh" % POINTS, *resample(f, g)))
            n_eq += 1

with open(os.path.join(out, "measurements.tsv"), "w", encoding="utf-8") as fh:
    for r in rows:
        fh.write("\t".join(x.replace("\t", " ") for x in r) + "\n")
with gzip.open(os.path.join(out, "measurements.bin.gz"), "wb", compresslevel=9) as fh:
    fh.write(b"".join(blobs))
print("%d entries (%d raw measurements + %d crinacle EQ results, %d skipped), %d sources -> %s" % (len(rows), len(rows) - n_eq, n_eq, skipped, len({r[0] for r in rows}), os.path.abspath(out)))
print("pack: %.1f MB, index: %.0f KB" % (os.path.getsize(os.path.join(out, "measurements.bin.gz")) / 1e6,
                                         os.path.getsize(os.path.join(out, "measurements.tsv")) / 1e3))
