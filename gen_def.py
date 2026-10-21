"""Erzeugt mss32.def: leitet alle Exporte der Original-DLL an mss32_orig.dll weiter.

Aufruf: python3 -I gen_def.py <pfad/zur/mss32.dll> <ausgabe.def>
"""
import re
import subprocess
import sys

src, dst = sys.argv[1], sys.argv[2]
out = subprocess.run(["i686-w64-mingw32-objdump", "-p", src], capture_output=True, text=True, check=True).stdout
section = out.split("[Ordinal/Name Pointer] Table", 1)[1]
rows = re.findall(r"^\s*\[\s*\d+\]\s+\+base\[\s*(\d+)\]\s+[0-9a-fA-F]+\s+(\S+)\s*$", section, re.M)
with open(dst, "w") as f:
    f.write("LIBRARY mss32\nEXPORTS\n")
    for ordinal, name in rows:
        f.write(f"  {name}=mss32_orig.{name} @{ordinal}\n")
print(len(rows), "Exporte")
