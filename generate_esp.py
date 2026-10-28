#!/usr/bin/env python3
"""NPC Pathing NG — plugin generator.

Builds NPCPathingNG.esp (ESL-flagged, Skyrim.esm master) plus Seq/NPCPathingNG.seq.
The ESP carries MCM-editable GLOB settings and the MCM Helper anchor quest
(MCM_ConfigBase script, no properties). The SKSE DLL reads the globals live,
so MCM changes apply instantly in-game.

Records:
  GLOB  0x800-0x80F, 0x811-0x816   settings (floats; bools as 0/1)
  QUST  0x810                       MCM anchor quest (VMAD: MCM_ConfigBase)

Run:  python generate_esp.py [output.esp]
"""
import struct, os, sys

PLUGIN_INDEX = 0x01000000  # one master (Skyrim.esm) -> our records live at index 01
NEXT_OBJECT_ID = 0x817       # HEDR stores the next local object ID, never a load-order-prefixed FormID

# ── low-level plugin encoding ────────────────────────────────────────────────
def zstr(s):
    return s.encode('ascii') + b'\x00'

def sub(tag, data):
    return tag.encode('ascii') + struct.pack('<H', len(data)) + data

def record(tag, form_id, flags, data):
    return (tag.encode('ascii') + struct.pack('<III', len(data), flags, form_id)
            + b'\x00\x00\x00\x00'            # VC info
            + struct.pack('<H', 44)           # form version
            + b'\x00\x00' + data)

def grup(label, *items):
    inner = b''.join(items)
    return (b'GRUP' + struct.pack('<I', 24 + len(inner)) + label
            + struct.pack('<i', 0) + b'\x00' * 8 + inner)

# ── VMAD (script data) — version 5, object format 2 ─────────────────────────
def wstr(s):
    b = s.encode('ascii')
    return struct.pack('<H', len(b)) + b

def vmad(script_name, props):
    props = sorted(props, key=lambda p: p[0].lower())
    blob = b''
    for name, form_id in props:
        blob += wstr(name) + struct.pack('<BB', 1, 1) + struct.pack('<HhI', 0, -1, form_id)
    script = wstr(script_name) + struct.pack('<B', 0) + struct.pack('<H', len(props)) + blob
    return struct.pack('<hhH', 5, 2, 1) + script  # version 5, objFormat 2, 1 script

# ── record builders ──────────────────────────────────────────────────────────
def make_glob(fid, edid, value):
    data = sub('EDID', zstr(edid)) + sub('FNAM', b'f') + sub('FLTV', struct.pack('<f', value))
    return record('GLOB', fid, 0, data)

def make_qust(fid):
    data = sub('EDID', zstr('NPCPathingNG_MCMQuest'))
    data += sub('VMAD', vmad('MCM_ConfigBase', []))
    data += sub('FULL', zstr('NPC Pathing NG'))
    # flags: Start Game Enabled (0x01) + hard stages run (0x10); flags2: Run Once
    dnam = struct.pack('<BBBB', 0x11, 0x01, 0x00, 0xFF) + struct.pack('<fI', 0.0, 0)
    data += sub('DNAM', dnam)
    return record('QUST', fid, 0, data)

# ── settings — MUST match Settings::BindGlobals in src/settings.cpp ─────────
GLOBS = [
    (0x800, 'NPNG_Enabled',          1.0),
    (0x801, 'NPNG_CheckInterval',    0.25),
    (0x802, 'NPNG_StuckThreshold',   4.0),
    (0x803, 'NPNG_StuckDistance',    4.0),
    (0x804, 'NPNG_Cooldown',         3.0),
    (0x805, 'NPNG_ActorsPerFrame',   10.0),
    (0x806, 'NPNG_EnableParkour',    1.0),
    (0x807, 'NPNG_IndoorMode',       0.0),   # no parkour indoors by default
    (0x808, 'NPNG_MaxClimbHeight',   130.0),  # steps/vaults/low ledges; raise to 250 for mountains
    (0x809, 'NPNG_TeleportFallback', 1.0),
    (0x80A, 'NPNG_SnapDistance',     100.0),
    (0x80B, 'NPNG_ExcludeInCombat',  0.0),   # combat pursuit ON by default
    (0x80C, 'NPNG_ExcludeFollowers', 0.0),   # followers included by default
    (0x80D, 'NPNG_ExcludeMounted',   1.0),
    (0x80E, 'NPNG_FollowerReplay',   1.0),
    (0x80F, 'NPNG_DebugLogging',     0.0),
    (0x811, 'NPNG_EVGTraversal',     1.0),  # 0x810 is the MCM quest
    (0x812, 'NPNG_TeleportEscalation', 3.0),
    (0x813, 'NPNG_ParkourMaxPlayerDistance', 1600.0),
    (0x814, 'NPNG_EnableLeap',       1.0),   # 3.0.0: own hurdle/vault/drop (off when another jump mod owns jumps)
    (0x815, 'NPNG_CompatMode',       0.0),   # 3.0.0: 0 Auto, 1 Force, 2 Passive
    (0x816, 'NPNG_FrameBudgetMs',    0.6),   # 3.0.0: soft per-frame time budget
]

# ── plugin assembly ──────────────────────────────────────────────────────────
def generate(esp_path):
    P = PLUGIN_INDEX

    r_glob = [make_glob(P | fid, e, v) for fid, e, v in GLOBS]
    r_qust = [make_qust(P | 0x810)]

    # HEDR counts records AND groups (everything after TES4). Up to 2.5.0 this was records only (20);
    # the game tolerates it, the ship-gate header audit does not.
    num_records = len(r_glob) + len(r_qust) + 2  # + the GLOB and QUST top-level groups
    tes4_data = (sub('HEDR', struct.pack('<fII', 1.71, num_records, NEXT_OBJECT_ID))
                 + sub('CNAM', zstr('GennyWoo'))  # public author name, not the local account
                 + sub('SNAM', zstr('NPC Pathing NG - NPC traversal + navmesh failsafe'))
                 + sub('MAST', zstr('Skyrim.esm'))
                 + sub('DATA', struct.pack('<Q', 0)))
    tes4 = record('TES4', 0, 0x00000200, tes4_data)  # 0x200 = ESL flag

    esp = tes4 + grup(b'GLOB', *r_glob) + grup(b'QUST', *r_qust)

    os.makedirs(os.path.dirname(esp_path) or '.', exist_ok=True)
    with open(esp_path, 'wb') as f:
        f.write(esp)
    print(f'Generated: {esp_path} ({len(esp):,} bytes, {num_records} records+groups)')

    seq_path = os.path.join(os.path.dirname(esp_path) or '.', 'Seq', 'NPCPathingNG.seq')
    os.makedirs(os.path.dirname(seq_path), exist_ok=True)
    with open(seq_path, 'wb') as f:
        f.write(struct.pack('<I', P | 0x810))
    print(f'Generated: {seq_path} (Start Game Enabled quest list)')

if __name__ == '__main__':
    generate(sys.argv[1] if len(sys.argv) > 1 else 'package/Data/NPCPathingNG.esp')
