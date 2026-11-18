#!/usr/bin/env python3
"""Convert a Monster Eyes style folder into firmware data.

Usage: eyepack.py <style_folder> <out.c> [--name NAME]

Reads config.eye plus the iris, sclera and eyelid bitmaps it names, and writes
a C file defining `const eye_style_t eye_style_<name>`, plus two preview
images next to it.

Monster Eyes stores the iris and sclera unrolled (x = angle, y = radius). The
firmware wants them flat, as seen looking straight at the pupil, so this
script rolls them back up:
  * iris:   a disc. The centre is the pupil colour; the iris occupies the
            ring from the hole radius to the edge.
  * sclera: a disc whose radius is the angle from the pupil axis, 0 to 90
            degrees. A style with a plain-colour sclera gets a 2x2 texture.

If the folder has *-symmetrical.bmp eyelid files they are used, since they
suit a single eye.

Needs Pillow and numpy.
"""
import argparse
import json
import math
import os
import re

import numpy as np
from PIL import Image

SOURCE_SIZE = 240.0          # Monster Eyes configs are in 240px-screen pixels
SCREEN_SIZE = 368.0          # our eye, for choosing texture sizes
TEXTURE_POOL = 120000        # pixels of RAM for iris + sclera; matches eye.h
DEFAULTS = {"eyeRadius": 125, "irisRadius": 60, "slitPupilRadius": 0,
            "pupilColor": [0, 0, 0], "backColor": [140, 40, 20],
            "irisColor": [0, 0, 255], "scleraColor": [255, 255, 255],
            "pupilMin": 0.20, "pupilMax": 0.55, "irisSpin": 0.0}


def load_config(folder):
    """config.eye is JSON-like but loose (comments, missing commas), so pick
    out "key": value pairs instead of parsing it strictly. Values in the
    "left" block override the top level: that is the eye we draw."""
    text = open(os.path.join(folder, "config.eye")).read()
    text = re.sub(r"//[^\n]*", "", text)
    pair = re.compile(r'"(\w+)"\s*:\s*(\[[^\]]*\]|"[^"]*"|[-+\w.]+)')

    def pairs(chunk):
        out = {}
        for key, raw in pair.findall(chunk):
            try:
                out[key] = json.loads(raw)
            except ValueError:
                out[key] = raw
        return out

    blocks = {}
    for side in ("left", "right"):
        m = re.search(r'"%s"\s*:\s*\{([^{}]*)\}' % side, text)
        blocks[side] = pairs(m.group(1)) if m else {}
        if m:
            text = text[:m.start()] + text[m.end():]
    cfg = dict(DEFAULTS)
    cfg.update(pairs(text))
    cfg.update(blocks["left"])
    return cfg


def find(folder, path):
    """Config paths look like 'hazel/iris.bmp'; the file sits in the folder."""
    return os.path.join(folder, os.path.basename(path))


def start_angle(cfg, key):
    """Where texture column 0 sits, in turns. Monster Eyes: half a turn by
    default; an explicit angle is given in 1024ths (int) or turns (float)."""
    if key not in cfg:
        return 0.5
    v = cfg[key]
    units = int(v) & 1023 if isinstance(v, int) else int(float(v) * 1024) & 1023
    return (1023 - units) / 1024.0


def sample(tex, col, row):
    """Bilinear sample; wraps in x (angle), clamps in y (radius)."""
    h, w, _ = tex.shape
    col = col % w
    row = np.clip(row, 0, h - 1)
    c0 = np.floor(col).astype(int) % w
    c1 = (c0 + 1) % w
    r0 = np.floor(row).astype(int)
    r1 = np.minimum(r0 + 1, h - 1)
    fc = (col - np.floor(col))[..., None]
    fr = (row - np.floor(row))[..., None]
    top = tex[r0, c0] * (1 - fc) + tex[r0, c1] * fc
    bot = tex[r1, c0] * (1 - fc) + tex[r1, c1] * fc
    return top * (1 - fr) + bot * fr


def disc_coords(n, start, mirror):
    """Radius (0..1 at the edge) and texture angle (0..1) per texel."""
    c = (n - 1) / 2.0
    y, x = np.mgrid[0:n, 0:n]
    dx, dy_up = x - c, c - y
    radius = np.hypot(dx, dy_up) / (n / 2.0)
    angle = (np.arctan2(dx, dy_up) / (2 * math.pi) + start) % 1.0   # clockwise from the top
    if mirror:
        angle = 1.0 - angle
    return radius, angle


def make_iris(tex, n, hole, pupil_rgb, start, mirror):
    h, w, _ = tex.shape
    radius, angle = disc_coords(n, start, mirror)
    t = np.clip((radius - hole) / (1 - hole), 0, 1)
    out = sample(tex, angle * w, (1 - t) * (h - 1))   # row 0 is the outer edge
    out[radius < hole] = pupil_rgb
    return out


def make_sclera(tex, n, iris_angle, start, mirror):
    h, w, _ = tex.shape
    radius, angle = disc_coords(n, start, mirror)
    phi = np.clip(radius, 0, 1) * (math.pi / 2)
    frac = np.clip((phi - iris_angle) / (math.pi / 2 - iris_angle), 0, 1)
    return sample(tex, angle * w, (1 - frac) * (h - 1))   # row 0 is the back of the eye


def lid_edges(path, upper):
    """Per column: the open and closed position of the lid edge. Upper lids
    sweep from the top of the white area (open) to its bottom (closed); lower
    lids the reverse. With no file, the lid stays out of the way."""
    if path is None:
        edge = np.zeros(2, int) if upper else np.ones(2, int)
        return edge, edge, 2, 2
    img = np.array(Image.open(path).convert("L")) > 127
    h, w = img.shape
    first = np.full(w, 0 if upper else h - 1)
    last = first.copy()
    for x in range(w):
        rows = np.nonzero(img[:, x])[0]
        if len(rows):
            first[x], last[x] = rows[0], rows[-1]
    return (first, last, w, h) if upper else (last, first, w, h)   # open, closed


def rgb565(rgb):
    rgb = np.clip(np.round(np.asarray(rgb, float)), 0, 255).astype(np.uint32)
    return ((rgb[..., 0] >> 3) << 11) | ((rgb[..., 1] >> 2) << 5) | (rgb[..., 2] >> 3)


def c_array(ctype, name, values, per_line=12, fmt="0x%04X"):
    flat = [int(v) for v in np.asarray(values).ravel()]
    lines = [", ".join(fmt % v for v in flat[i:i + per_line])
             for i in range(0, len(flat), per_line)]
    return "static const %s %s[%d] = {\n  %s\n};\n" % (
        ctype, name, len(flat), ",\n  ".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("folder")
    ap.add_argument("out")
    ap.add_argument("--name")
    args = ap.parse_args()

    name = args.name or os.path.basename(os.path.normpath(args.folder))
    ident = re.sub(r"\W", "_", name)
    cfg = load_config(args.folder)
    eye_radius = float(cfg["eyeRadius"])
    iris_radius = min(float(cfg["irisRadius"]), eye_radius)
    slit = float(cfg["slitPupilRadius"])
    pupil_min, pupil_max = sorted((float(cfg["pupilMin"]), float(cfg["pupilMax"])))

    # A big iris needs a big texture; its sclera is then small and gets less.
    iris_on_screen = iris_radius / SOURCE_SIZE * SCREEN_SIZE * 2
    iris_n, sclera_n = (192, 256) if iris_on_screen <= 200 else \
                       (256, 192) if iris_on_screen <= 280 else (320, 128)
    # The iris ring starts where the smallest pupil ends. A slit pupil is
    # drawn over the iris, so the ring runs nearly to the centre.
    hole = 0.04 if slit > 0 else min(max(pupil_min, 0.04), 0.40)

    if "irisTexture" in cfg:
        src = np.array(Image.open(find(args.folder, cfg["irisTexture"])).convert("RGB"), float)
    else:
        src = np.full((2, 2, 3), cfg["irisColor"], float)
    iris = make_iris(src, iris_n, hole, cfg["pupilColor"],
                     start_angle(cfg, "irisAngle"), bool(cfg.get("irisMirror", False)))

    if "scleraTexture" in cfg:
        src = np.array(Image.open(find(args.folder, cfg["scleraTexture"])).convert("RGB"), float)
        sclera = make_sclera(src, sclera_n, math.asin(iris_radius / eye_radius),
                             start_angle(cfg, "scleraAngle"), bool(cfg.get("scleraMirror", False)))
    else:
        sclera_n = 2
        sclera = np.full((2, 2, 3), cfg["scleraColor"], float)
    assert iris_n * iris_n + sclera_n * sclera_n <= TEXTURE_POOL

    def lid_file(key):
        if key not in cfg:
            return None
        path = find(args.folder, cfg[key])
        alt = path.replace(".bmp", "-symmetrical.bmp")
        return alt if os.path.exists(alt) else path

    uo, uc, lid_n, lid_h = lid_edges(lid_file("upperEyelid"), upper=True)
    lo, lc, lower_n, lower_h = lid_edges(lid_file("lowerEyelid"), upper=False)
    if (lower_n, lower_h) != (lid_n, lid_h):       # one lid file missing: stretch the stub
        if lid_n == 2:
            uo, uc, lid_n, lid_h = np.zeros(lower_n, int), np.zeros(lower_n, int), lower_n, lower_h
        else:
            lo = lc = np.full(lid_n, lid_h - 1)

    with open(args.out, "w") as f:
        f.write("// Generated by tools/eyepack.py from art/%s. Do not edit.\n" % name)
        f.write('#include "eye.h"\n\n')
        f.write(c_array("uint16_t", "iris", rgb565(iris)))
        f.write(c_array("uint16_t", "sclera", rgb565(sclera)))
        for label, table in (("upper_open", uo), ("upper_closed", uc),
                             ("lower_open", lo), ("lower_closed", lc)):
            f.write(c_array("uint8_t", label, table, per_line=20, fmt="%d"))
        f.write("\nconst eye_style_t eye_style_%s = {\n" % ident)
        f.write('    .name = "%s",\n' % name)
        f.write("    .iris_n = %d, .sclera_n = %d,\n" % (iris_n, sclera_n))
        f.write("    .iris = iris, .sclera = sclera,\n")
        f.write("    .lid_n = %d, .lid_h = %d,\n" % (lid_n, lid_h))
        f.write("    .upper_open = upper_open, .upper_closed = upper_closed,\n")
        f.write("    .lower_open = lower_open, .lower_closed = lower_closed,\n")
        f.write("    .back_color = 0x%04X, .lid_color = 0x0000, .pupil_color = 0x%04X,\n" % (
            rgb565(cfg["backColor"]), rgb565(cfg["pupilColor"])))
        f.write("    .eye_radius = %.5ff, .iris_radius = %.5ff, .iris_hole = %.2ff,\n" % (
            eye_radius / SOURCE_SIZE, iris_radius / SOURCE_SIZE, hole))
        f.write("    .pupil_min = %.3ff, .pupil_max = %.3ff,\n" % (pupil_min, pupil_max))
        f.write("    .slit_radius = %.5ff, .spin_rpm = %.2ff,\n};\n" % (
            slit / SOURCE_SIZE, float(cfg["irisSpin"])))

    base = os.path.splitext(args.out)[0]
    Image.fromarray(np.clip(iris, 0, 255).astype(np.uint8)).save(base + "_iris.png")
    Image.fromarray(np.clip(sclera, 0, 255).astype(np.uint8)).save(base + "_sclera.png")
    print("wrote %s  (iris %d, sclera %d%s%s)" % (
        args.out, iris_n, sclera_n, ", slit" if slit > 0 else "",
        ", spin %g" % float(cfg["irisSpin"]) if float(cfg["irisSpin"]) else ""))


if __name__ == "__main__":
    main()
