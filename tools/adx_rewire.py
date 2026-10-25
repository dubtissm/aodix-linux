#!/usr/bin/env python3
# Test helper: rewrites the routing (wires), the notes and the cue range of an Aodix project (.adx, see docs/adx-spec.txt).
# The instances must exist in the project already (load them in Aodix and save); their pin and parameter counts are taken
# from the extended region of the file.
#
#   adx_rewire.py in.adx out.adx routing.json
#
# routing.json: {"wires": [[src_instance, src_pin, dst_instance, dst_pin, gain], ...],      dst_instance 256 = master output
#                "midi":  [[src_instance, dst_instance, gain], ...],
#                "notes": [[instance, track, pos, length, note, velocity], ...],
#                "cue_end": positions}
import json, struct, sys

src, dst, cfg_path = sys.argv[1:4]
cfg = json.load(open(cfg_path))
d = open(src, 'rb').read()

PROJ = 8
PROJ_SIZE = 0x2C6058
ext_offset = struct.unpack_from('<i', d, PROJ + 0x2C6054)[0]
fallback = [struct.unpack_from('<BBH', d, ext_offset + 4 * i) for i in range(256)]  # outputs, programs, params

pos = PROJ + PROJ_SIZE
num_events = struct.unpack_from('<i', d, pos)[0]
inst_start = pos + 4 + 16 * num_events

# parse instances
p = inst_start
instances = []
for i in range(256):
    fmt = d[p]
    if not fmt:
        instances.append(None)
        p += 1
        continue
    head = d[p:p + 1 + 260 + 4 + 4 + 4 + 1 + 1 + 32]
    p += len(head)
    nout = fallback[i][0]
    for _ in range(nout + 1):              # audio outputs + midi output
        nw = struct.unpack_from('<i', d, p)[0]
        p += 4 + 8 * nw
    prog = d[p:p + 4]
    p += 4
    if fmt == 2:
        size = struct.unpack_from('<i', d, p)[0]
        data = d[p:p + 4 + size]
    elif fmt == 3:
        data = b''
    elif fmt == 4:
        data = d[p:p + 32 + 4 * fallback[i][2]]
    else:
        sys.exit('format 1 instances not supported')
    p += len(data)
    midicc = d[p:p + fallback[i][2]]
    p += len(midicc)
    instances.append(dict(head=head, nout=nout, prog=prog, data=data, midicc=midicc))
tail = d[p:ext_offset]                        # master input pins + routing view
ext = d[ext_offset:]

# new wires
audio = {}
for s, sp, di, dp, g in cfg.get('wires', []):
    audio.setdefault((s, sp), []).append((di, dp, g))
midi = {}
for s, di, g in cfg.get('midi', []):
    midi.setdefault(s, []).append((di, 0, g))

out_inst = bytearray()
for i, inst in enumerate(instances):
    if inst is None:
        out_inst += b'\0'
        continue
    out_inst += inst['head']
    for o in range(inst['nout']):
        w = audio.get((i, o), [])
        out_inst += struct.pack('<i', len(w)) + b''.join(struct.pack('<hhf', a, b, c) for a, b, c in w)
    w = midi.get(i, [])
    out_inst += struct.pack('<i', len(w)) + b''.join(struct.pack('<hhf', a, b, c) for a, b, c in w)
    out_inst += inst['prog'] + inst['data'] + inst['midicc']

# project: cue range of pattern 0
proj = bytearray(d[PROJ:PROJ + PROJ_SIZE])
pat0 = 0x48
struct.pack_into('<3i', proj, pat0 + 0x2420, 0, cfg.get('cue_end', 192 * 8), 0)

# notes: pos, len, pat, trk, typ, szd, instance, channel, note, velocity
events = b''.join(struct.pack('<iiBBBBBBBB', ps, ln, 0, trk, 0, 1, inst, 0, note, vel)
                  for inst, trk, ps, ln, note, vel in cfg.get('notes', []))
n_ev = len(cfg.get('notes', []))

body = bytearray(d[:PROJ]) + proj + struct.pack('<i', n_ev) + events + out_inst + tail
struct.pack_into('<i', body, PROJ + 0x2C6054, len(body))     # new extended region offset
open(dst, 'wb').write(body + ext)
print('instances:', [(i, x['nout'], fallback[i][2]) for i, x in enumerate(instances) if x])
