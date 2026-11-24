#!/usr/bin/env python3
"""Regenerates engine/src/formations_table.inc from assets/data/formations.json (run from the repo root)."""
import json
d = json.load(open("assets/data/formations.json"))['formations']
out = ["// Generated from assets/data/formations.json by tools/gen_formations.py. Do not edit by hand.\n"]
for name, slots in d.items():
    s = ", ".join("{Role::%s, %.2ff, %.2ff}" % (x['role'], x['x'], x['y']) for x in slots)
    out.append('    {"%s", {%s}},\n' % (name, s))
open('engine/src/formations_table.inc', 'w').write("".join(out))
