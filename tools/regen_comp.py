#!/usr/bin/env python3
"""Regenerates the circuit compensation tables in Source/dsp/CompTables.h.
Usage: regen_comp.py <build dir>   (run from the project folder)"""
import re, subprocess, sys
build = sys.argv[1] if len(sys.argv) > 1 else "build"
path = "Source/dsp/CompTables.h"
src = open(path).read()
a = src.index("// GENERATED-BEGIN") + len("// GENERATED-BEGIN\n")
b = src.index("// GENERATED-END")
empty = "".join(f"inline constexpr Table {n} {{}};\n" for n in ["triode", "diode", "hysteresis", "iron", "stack"])
open(path, "w").write(src[:a] + empty + src[b:])
subprocess.run(["ninja", "-C", build, "KlingeTests"], check=True, stdout=subprocess.DEVNULL)
gen = subprocess.run([f"{build}/KlingeTests_artefacts/Release/KlingeTests", "gen-comp"], check=True, capture_output=True, text=True).stdout.strip()
src = open(path).read()
a = src.index("// GENERATED-BEGIN") + len("// GENERATED-BEGIN\n")
b = src.index("// GENERATED-END")
open(path, "w").write(src[:a] + gen + "\n" + src[b:])
subprocess.run(["ninja", "-C", build, "KlingeTests"], check=True, stdout=subprocess.DEVNULL)
print("tables regenerated")
