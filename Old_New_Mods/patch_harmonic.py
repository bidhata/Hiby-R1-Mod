#!/usr/bin/env python3
# Exposes HiBy's hidden "Harmonic Controller" (Darwin menu, setting 0x6b) on the R1.
#
# The effect itself already runs in the analog output path: with level L > 0 it
# scales only the negative half of each sample by (65536 - L) / 65536, which adds
# even-order (mostly 2nd) harmonics. The R1 just has no menu to reach it, and the
# saved level is never re-applied after a reboot. This patch:
#
#   1. Points the System settings row "operating_instruction" (online manual) at
#      the Harmonic Controller slider dialog instead.
#   2. Hooks the boot-time settings load so the saved level is applied at startup.
#   3. Rescales the effect from (65536 - L) / 65536 to (SCALE - L) / SCALE. Stock
#      tops out at a ~-62 dB 2nd harmonic, which is inaudible. With SCALE = 512,
#      L = 16 gives ~-44 dB (0.7%), L = 64 ~-32 dB (2.6%), L = 255 ~-19 dB (10.6%).
#
# Everything is located by instruction signature, not fixed offsets.
# The new boot code goes in the zero padding at the end of the executable segment.
#
# Usage:  python3 patch_harmonic.py <hiby_player_binary>   (patched in place)
import struct
import sys

BASE = 0x400000
SCALE = 512  # power of two so 1/SCALE is exact; must be > 255


def main():
    if len(sys.argv) < 2:
        print("Usage: patch_harmonic.py <hiby_player_binary>")
        sys.exit(1)

    target_file = sys.argv[1]
    with open(target_file, 'rb') as f:
        data = bytearray(f.read())

    def word(off):
        return struct.unpack_from('<I', data, off)[0]

    def put(off, w):
        struct.pack_into('<I', data, off, w)

    def jal_target(w):
        return (w & 0x03ffffff) << 2

    def jal(va):
        return 0x0c000000 | (va >> 2)

    def find_words(seq, start=0, end=None):
        # All 4-byte aligned offsets where the word sequence (None = wildcard) occurs.
        end = len(data) if end is None else end
        hits = []
        for off in range(start, end - 4 * len(seq), 4):
            if all(s is None or word(off + 4 * i) == s for i, s in enumerate(seq)):
                hits.append(off)
        return hits

    def func_start(off):
        # Walk back to the prologue (addiu sp,sp,-N).
        for p in range(off, max(off - 0x400, 0), -4):
            w = word(p)
            if (w & 0xffff0000) == 0x27bd0000 and (w & 0x8000):
                return p
        print(f"Error: no function prologue before {hex(off)}")
        sys.exit(1)

    def unique(hits, what):
        if len(hits) != 1:
            print(f"Error: {what} found {len(hits)} times, expected 1. Unknown or already patched binary?")
            sys.exit(1)
        return hits[0]

    # Executable segment end (first PT_LOAD), from the ELF program headers.
    e_phoff, = struct.unpack_from('<I', data, 0x1c)
    e_phentsize, e_phnum = struct.unpack_from('<HH', data, 0x2a)
    text_ph = None
    for i in range(e_phnum):
        ph = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, p_flags = struct.unpack_from('<IIIIIII', data, ph)
        if p_type == 1 and p_flags & 1:
            text_ph = (ph, p_offset, p_vaddr, p_filesz, p_memsz)
            break
    if text_ph is None or text_ph[2] != BASE or text_ph[1] != 0:
        print("Error: unexpected ELF layout")
        sys.exit(1)
    text_end = text_ph[3]

    # 1. Harmonic slider dialog: sets dialog setting id 0x6b and max 0xff.
    #    li v0,107 ; sw v0,188(s0) ; li v0,255 ; sw v0,216(s0)
    dlg_sig = unique(find_words([0x2402006b, 0xae0200bc, 0x240200ff, 0xae0200d8], end=text_end),
                     "harmonic dialog")
    dlg = func_start(dlg_sig)
    # Settings getter: the jal with li a0,0x6b right before it or in its delay slot.
    getter = None
    for p in range(dlg_sig, dlg_sig + 0x40, 4):
        if (word(p) >> 26) == 3 and 0x2404006b in (word(p - 4), word(p + 4)):
            getter = jal_target(word(p))
            break
    if getter is None:
        print("Error: settings getter call not found in harmonic dialog")
        sys.exit(1)
    print(f"[+] Harmonic dialog at {hex(dlg + BASE)}, settings getter {hex(getter)}")

    # 2. Harmonic apply: sltiu v0,a0,256 ; ... ; jal <writer>, writer starts sltiu v0,a0,257.
    apply_fn = None
    for off in find_words([0x2c820100], end=text_end):
        if word(off + 20) == 0x27bdffe0 and word(off + 24) == 0xafbf001c and (word(off + 28) >> 26) == 3:
            if word(jal_target(word(off + 28)) - BASE) == 0x2c820101:
                if apply_fn is not None:
                    print("Error: harmonic apply function is ambiguous")
                    sys.exit(1)
                apply_fn = off + BASE
    if apply_fn is None:
        print("Error: harmonic apply function not found")
        sys.exit(1)
    print(f"[+] Harmonic apply at {hex(apply_fn)}")

    # 3. System settings tap handler, row 0x21 (operating_instruction):
    #    li v1,33 ; beq v0,v1,<branch> ; li a2,128
    row = unique([o for o in find_words([0x24030021, None, 0x24060080], end=text_end)
                  if (word(o + 4) & 0xffff0000) == 0x10430000], "operating_instruction branch")
    disp = struct.unpack('<h', struct.pack('<H', word(row + 4) & 0xffff))[0]
    branch = row + 8 + disp * 4
    # Brightness row in the same handler: move a1,s2 ; jal dlg ; move a0,zero ; b <epilogue> ; lw ra,212(sp)
    epi = None
    for o in find_words([0x02402825, None, 0x00002025, None, 0x8fbf00d4], max(row - 0x1000, 0), row + 0x1000):
        if (word(o + 4) >> 26) == 3 and (word(o + 12) & 0xffff0000) == 0x10000000:
            d = struct.unpack('<h', struct.pack('<H', word(o + 12) & 0xffff))[0]
            epi = o + 16 + d * 4
            break
    if epi is None:
        print("Error: settings handler epilogue pattern not found")
        sys.exit(1)
    if word(branch) == 0x02402825 and word(branch + 4) == jal(dlg + BASE):
        print("[*] Settings row already patched.")
    else:
        b_off = (epi - (branch + 16)) // 4
        put(branch, 0x02402825)                       # move a1,s2
        put(branch + 4, jal(dlg + BASE))              # jal harmonic dialog
        put(branch + 8, 0x00002025)                   # move a0,zero
        put(branch + 12, 0x10000000 | (b_off & 0xffff))  # b epilogue
        put(branch + 16, 0x8fbf00d4)                  # lw ra,212(sp)
        print(f"[+] Row 0x21 at {hex(branch + BASE)} now opens the harmonic dialog")

    # 4. Boot hook. The settings loader is the function that allocates the
    #    0xb08-byte settings block (li a0,2824); it is called exactly once at startup.
    loaders = {func_start(o) + BASE for o in find_words([0x24040b08], end=text_end)}
    cave = (text_end + 0xf) & ~0xf
    call = None
    for ld in loaders:
        sites = find_words([jal(ld), 0x00000000], end=text_end)
        if len(sites) == 1:
            call, loader = sites[0], ld
    hooked = find_words([0x27bdffe8, 0xafbf0014, None, 0x00000000, jal(getter), 0x2404006b, jal(apply_fn)],
                        start=0x470000, end=0x476000)
    if hooked:
        print("[*] Boot hook already installed.")
    else:
        if call is None:
            print("Error: settings loader call site not found")
            sys.exit(1)
        stub = [
            0x27bdffe8,           # addiu sp,sp,-24
            0xafbf0014,           # sw    ra,20(sp)
            jal(loader),          # jal   settings loader (original call)
            0x00000000,           # nop
            jal(getter),          # jal   getter
            0x2404006b,           # li    a0,0x6b
            jal(apply_fn),        # jal   harmonic apply
            0x00402025,           # move  a0,v0
            0x8fbf0014,           # lw    ra,20(sp)
            0x03e00008,           # jr    ra
            0x27bd0018,           # addiu sp,sp,24
        ]
        need = cave + 4 * len(stub)
        if need > 0x476000 or any(data[cave:need]) or any(data[text_end:cave]):
            print("Error: no free padding after the executable segment")
            sys.exit(1)
        for i, w in enumerate(stub):
            put(cave + 4 * i, w)
        put(call, jal(cave + BASE))
        # Make the cave formally part of the executable segment.
        ph = text_ph[0]
        struct.pack_into('<II', data, ph + 16, need, need)
        print(f"[+] Boot hook: settings load at {hex(call + BASE)} -> cave {hex(cave + BASE)}")

    # 5. Strength. Stock gain is (65536 - L) / 65536, so even L=255 only trims the
    #    negative half-wave by 0.39% (2nd harmonic ~ -62 dB, inaudible). Use
    #    (SCALE - L) / SCALE instead: lui v1,0x1 -> li v1,SCALE, and point the
    #    1/65536 load at a 1/SCALE float stored after the boot stub.
    writer = jal_target(word(apply_fn - BASE + 28)) - BASE
    if find_words([0x24030000 | SCALE, 0x00641823, 0x44830000], writer, writer + 0x60):
        print("[*] Harmonic strength already patched.")
    else:
        o = unique([h for h in find_words([0x3c030001, 0x00641823, 0x44830000], writer, writer + 0x60)
                    if word(h + 12) >> 16 == 0x3c03 and word(h + 16) >> 16 == 0xc461],
                   "harmonic gain sequence")
        ph = text_ph[0]
        seg_end, = struct.unpack_from('<I', data, ph + 16)
        k_off = (seg_end + 3) & ~3
        if k_off + 4 > 0x476000 or any(data[seg_end:k_off + 4]):
            print("Error: no free padding for the strength constant")
            sys.exit(1)
        put(k_off, struct.unpack('<I', struct.pack('<f', 1.0 / SCALE))[0])
        k_va = k_off + BASE
        put(o, 0x24030000 | SCALE)                              # li   v1,SCALE
        put(o + 12, 0x3c030000 | ((k_va + 0x8000) >> 16))      # lui  v1,%hi(k)
        put(o + 16, 0xc4610000 | (k_va & 0xffff))              # lwc1 $f1,%lo(k)(v1)
        struct.pack_into('<II', data, ph + 16, k_off + 4, k_off + 4)
        print(f"[+] Harmonic strength: scale 65536 -> {SCALE} (constant at {hex(k_va)})")

    with open(target_file, 'wb') as f:
        f.write(data)

    print("[+] Patch applied successfully!")


if __name__ == '__main__':
    main()
