#!/usr/bin/env python3
# Removes the brightness clamp in hiby_player (values 1-4 forced -> 5),
# so the minimum can drop down to 1. Locates the clamp by its instruction
# signature, so it works on stock and community variants (Sorting, Playlist, etc.).
#
# Usage:  python3 patch_brightness.py hiby_player
# Output: hiby_player_patched
import sys, re

src = sys.argv[1] if len(sys.argv) > 1 else "hiby_player"
d = bytearray(open(src, "rb").read())

# Clamp idiom (MIPS LE):
#   addiu v1,v0,-1   ff ff 43 24
#   sltiu v1,v1,4    04 00 63 2c   <-- patch immediate 4 -> 0
#   jal   <write>    ?? ?? ?? 0c
#   movz  a1,v0,v1   0a 28 43 00   (delay slot)
sig = re.compile(rb"\xff\xff\x43\x24\x04\x00\x63\x2c...\x0c\x0a\x28\x43\x00", re.DOTALL)
hits = [m.start() for m in sig.finditer(d)]

if len(hits) == 0:
    sys.exit("ERROR: clamp signature not found - unknown/incompatible binary")
if len(hits) > 1:
    sys.exit(f"ERROR: signature found {len(hits)} times, ambiguous: {[hex(h) for h in hits]}")

sltiu_off = hits[0] + 4          # skip the addiu, point at the sltiu word
assert d[sltiu_off] == 0x04, f"unexpected byte {d[sltiu_off]:#x} at {sltiu_off:#x}"
d[sltiu_off] = 0x00              # immediate 4 -> 0  => clamp never triggers

open("hiby_player_patched", "wb").write(d)
print(f"clamp found at offset {sltiu_off:#x}")
print(f"sltiu v1,v1,4 -> sltiu v1,v1,0  (byte {sltiu_off:#x}: 04 -> 00)")
print("written: hiby_player_patched")
