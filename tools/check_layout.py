#!/usr/bin/env python3
"""Verifies that engine/include/realxi/snapshot.h and app/.../engine/Snapshot.kt agree (offsets, sizes, event ids)."""
import re, sys

h = open('engine/include/realxi/snapshot.h').read()
k = open('app/src/main/java/com/realxi/engine/Snapshot.kt').read()

# C++ enum evaluation
body = re.search(r'enum : int \{(.*?)\};', h, re.S).group(1)
body = re.sub(r'//[^\n]*', '', body)
vals, cur, names = {}, 0, []
for item in [x.strip() for x in body.replace('\n', ' ').split(',') if x.strip()]:
    if '=' in item:
        name, expr = [t.strip() for t in item.split('=')]
        cur = eval(expr, {}, vals)
    else:
        name = item
    vals[name] = cur
    cur += 1

def camel_to_const(n):
    n = n[1:]  # drop leading k
    return re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', n).upper()

snapk = k[k.index('object Snap'):k.index('object Phase')]
kt = {m.group(1): int(m.group(2)) for m in re.finditer(r'const val (\w+) = (\d+)\b', snapk)}
# evaluate simple derived Kotlin constants
for m in re.finditer(r'const val (\w+) = ([A-Z_]+ [+*] [^\n]+)', snapk):
    try:
        kt[m.group(1)] = eval(m.group(2), {}, kt)
    except Exception:
        pass

mapping = {
    'kHeaderSize': 'HEADER_SIZE', 'kPlayerStride': 'PLAYER_STRIDE', 'kPlayers': 'PLAYERS', 'kStatsStride': 'STATS_STRIDE',
    'kStats': 'STATS', 'kTargets': 'TARGETS', 'kPredict': 'PREDICT', 'kFloats': 'FLOATS', 'kMaxEvents': 'MAX_EVENTS',
}
bad = 0
for n, v in vals.items():
    kn = mapping.get(n)
    if kn is None:
        kn = camel_to_const(n)
        kn = {'DRILL_LAST': 'DRILL_LAST', 'TAKER': 'TAKER'}.get(kn, kn)
    if kn not in kt:
        print('missing in Kotlin:', n, '->', kn); bad += 1
    elif kt[kn] != v:
        print('MISMATCH', n, v, 'vs Kotlin', kn, kt[kn]); bad += 1

# Event / phase / command ids
types = re.search(r'enum class EventType : int32_t \{(.*?)\};', open('engine/include/realxi/types.h').read(), re.S).group(1)
ev = [t.strip() for t in types.replace('\n', ' ').split(',') if t.strip()]
ev_ids = {n: i for i, n in enumerate(ev)}
for m in re.finditer(r'const val (\w+) = (\d+)', k[k.index('object Ev'):k.index('object Cmd')]):
    cn = m.group(1).replace('_', '').lower()
    match = [n for n in ev_ids if n.lower() == cn]
    if match and ev_ids[match[0]] != int(m.group(2)):
        print('EVENT MISMATCH', m.group(1)); bad += 1
print('layout OK' if bad == 0 else f'{bad} problems')
sys.exit(1 if bad else 0)
