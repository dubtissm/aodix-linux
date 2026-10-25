#!/usr/bin/env python3
# Writes a small Aodix project (.adx) following docs/adx-spec.txt, used to test the project loader.
import struct, sys

out = sys.argv[1] if len(sys.argv) > 1 else 'test.adx'
ppqn = 192

def cstr(s, n):
    b = s.encode('latin-1')[:n-1]
    return b + b'\0' * (n - len(b))

proj = bytearray()
proj += cstr('Linux Test', 32) + cstr('ADX loader test', 32)
proj += struct.pack('<d', 128.0)
for p in range(256):
    pat = bytearray()
    pat += cstr('Arrange' if p == 0 else ('Beat' if p == 1 else '---'), 32)
    for t in range(256):
        name = ['Kick', 'Snare', 'Hats', 'Bass'][t] if (p == 0 and t < 4) else '---'
        pat += cstr(name, 32) + bytes([0, 0, 0, 0])
    pat += struct.pack('<4i', 0, ppqn * 4, 0, 0)            # cue start, end, stop, reserved
    pat += struct.pack('<4i', 0, max(1, ppqn // 64), ppqn // 16, 0)  # usr pos, zoom, pre-release, mode
    pat += b'\0' * 32
    for m in range(256):
        pat += struct.pack('<2i', ppqn * m if 0 < m < 4 else 0, 1 if 0 < m < 4 else 0)
    assert len(pat) == 0x2C60
    proj += pat
proj += struct.pack('<4i', 4, 4, ppqn, 0)   # numerator, denominator, ppqn, ext offset (patched)
assert len(proj) == 0x2C6058

events = []
# notes: (pos, len, pat, trk, typ, szd, da0 instance, da1 channel, da2 note, da3 velocity)
for beat in range(8):
    events.append((beat * ppqn, ppqn // 2, 0, 0, 0, 1, 0, 0, 36, 120))
    if beat % 2 == 1:
        events.append((beat * ppqn, ppqn // 4, 0, 1, 0, 1, 1, 0, 38, 110))
    for h in range(2):
        events.append((beat * ppqn + h * ppqn // 2, ppqn // 8, 0, 2, 0, 1, 2, 0, 42, 80 + h * 20))
bass = [36, 36, 43, 41]
for i, n in enumerate(bass):
    events.append((i * ppqn * 2, ppqn * 2 - 10, 0, 3, 0, 1, 3, 0, n, 100))
# a tempo event
events.append((ppqn * 4, 0, 0, 0, 5, 0, 130, 0, 0, 0))

data = bytearray()
data += b'4XDA'     # 'ADX4' multi-char constant written little endian
data += struct.pack('<i', 4203)
proj_ofs = len(data)
data += proj
data += struct.pack('<i', len(events))
for e in events:
    data += struct.pack('<iiBBBBBBBB', *e)
data += b'\0' * 256                  # instance table: no instances
data += struct.pack('<i', 0) * 32     # master input pins: no wires
data += struct.pack('<6i', 0, 0, 0, 256, -256, -128)
ext = len(data)
data += b'\0' * 0x400                 # instance fallback table
struct.pack_into('<i', data, proj_ofs + 0x2C6054, ext)
open(out, 'wb').write(data)
print(out, len(data), 'bytes,', len(events), 'events')
