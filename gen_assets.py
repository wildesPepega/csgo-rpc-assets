"""Erzeugt die Discord-Art-Assets (1024x1024) aus den Spieldateien in assets/ und die Key-Liste assets_keys.h.

Aufruf: python3 -I gen_assets.py
Die DLL laedt die Bilder per URL aus dem Repo (assets/ auf GitHub, siehe kAssetBaseUrl).
"""
import os
import struct
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "..", "csgo")
OUT = os.path.join(HERE, "assets")
FONT = "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"
os.makedirs(OUT, exist_ok=True)


def square(src, dst, size=1024):
    """Mittiger Quadrat-Ausschnitt, auf size x size skaliert."""
    subprocess.run(["magick", src, "-resize", f"{size}x{size}^", "-gravity", "center", "-extent", f"{size}x{size}",
                    "-quality", "90", dst], check=True)


def read_vpk_dir(path):
    f = open(path, "rb")
    _, ver, tree = struct.unpack("<III", f.read(12))
    if ver == 2:
        f.read(16)
    data = f.read(tree)
    pos = 0
    entries = {}

    def cs():
        nonlocal pos
        e = data.index(b"\0", pos)
        s = data[pos:e].decode("utf8", "replace")
        pos = e + 1
        return s

    while (ext := cs()):
        while (d := cs()):
            while (n := cs()):
                _, pre, idx, off, ln = struct.unpack_from("<IHHII", data, pos)
                pos += 18
                entries[f"{d}/{n}.{ext}"] = (idx, off, ln, data[pos:pos + pre])
                pos += pre
    return entries


keys = set()

# 1) Map-Screenshots: lose maps/*.jpg, sonst aus dem VPK (panorama 1080p)
maps_dir = os.path.join(GAME, "maps")
for fn in sorted(os.listdir(maps_dir)):
    if fn.endswith(".jpg"):
        key = fn[:-4].lower()
        square(os.path.join(maps_dir, fn), os.path.join(OUT, key + ".jpg"))
        keys.add(key)

vpk_entries = read_vpk_dir(os.path.join(GAME, "supremacy_dir.vpk"))
prefix = "materials/panorama/images/map_icons/screenshots/1080p/"
tmp = os.path.join(OUT, "_tmp.png")
for name, (idx, off, ln, pre) in sorted(vpk_entries.items()):
    if not (name.startswith(prefix) and name.endswith(".png")):
        continue
    key = name[len(prefix):-4].lower()
    if key in keys or len(key) > 32:
        continue
    with open(os.path.join(GAME, f"supremacy_{idx:03d}.vpk"), "rb") as a:
        a.seek(off)
        png = pre + a.read(ln)
    with open(tmp, "wb") as t:
        t.write(png)
    square(tmp, os.path.join(OUT, key + ".jpg"))
    keys.add(key)
os.remove(tmp)

# 2) Projekt-Logo (Icon des Supremacy-Launchers) auf dunklem Grund
subprocess.run(["magick", os.path.join(HERE, "icons_src", "logo.png"), "-background", "#0d0d12", "-alpha", "remove", "-filter",
                "Lanczos", "-resize", "1024x1024", os.path.join(OUT, "logo.png")], check=True)
keys.add("logo")

# 3) Team-Icons (icons_src/) und Spectator-Auge
for key in ("ct", "t"):
    subprocess.run(["magick", os.path.join(HERE, "icons_src", key + ".png"), "-filter", "Lanczos", "-resize", "512x512",
                    os.path.join(OUT, key + ".png")], check=True)
    keys.add(key)

EYE_SVG = """<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
<circle cx="256" cy="256" r="240" fill="#141a24" stroke="#6b7f99" stroke-width="16"/>
<path d="M80 256 Q256 96 432 256 Q256 416 80 256 Z" fill="#e8edf4"/>
<circle cx="256" cy="256" r="74" fill="#4a7fd4"/>
<circle cx="256" cy="256" r="36" fill="#0b0e14"/>
<circle cx="276" cy="236" r="14" fill="#ffffff"/>
</svg>"""
svg = os.path.join(OUT, "_eye.svg")
with open(svg, "w") as f:
    f.write(EYE_SVG)
subprocess.run(["rsvg-convert", svg, "-o", os.path.join(OUT, "spectator.png")], check=True)
os.remove(svg)
keys.add("spectator")

with open(os.path.join(HERE, "assets_keys.h"), "w") as h:
    h.write("// Generiert von gen_assets.py - Dateinamen der Bilder im Assets-Repo.\n")
    h.write("static const char* const kAssetFiles[] = {\n")
    for fn in sorted(f for f in os.listdir(OUT) if f.endswith((".jpg", ".png"))):
        h.write(f'    "{fn}",\n')
    h.write("};\n")
print(len(keys), "Assets in", OUT)
