#!/usr/bin/env python3
"""
Source-level fix for the HiBy R1 album-art SIGSEGV crash.

Firmware base: 1.7b1

Root cause: in album_cover_parser (fcn.00427ee0) the decode result's image-pointer
field result[2] is stored into the list item's +8 field WITHOUT validation. On a
failed decode (the decoder's malloc fails under the device's tiny free RAM) that
field holds the JPEG *file size* -- a small, invalid pointer -- which the drawing
code later dereferences (lw a0,0x50(item+8)) -> SIGSEGV -> reboot.

Fix: intercept that one store. On a failed decode the image-pointer field result[2]
and the file-size field result[1] hold the same value (the file size); on success
they differ (result[2] is a real address). The hook compares them and stores 0 when
they are equal. The drawing path already skips NULL covers, so this single patch
fixes the crash for EVERY view that draws a cover (~50 draw sites), not just the
album list.  See README.md (section 6) for the full rationale, including why a plain
`< 0x00400000` range check was rejected (it blanks valid covers).

The store is located by byte SIGNATURE (not a hard-coded address) so the patch is
resilient to minor address shifts between firmware builds. Idempotent.

Usage: python3 patch_player_source_fix.py <path_to_hiby_player>
"""
import struct, sys, os

TEXT_BASE = 0x00400000     # MIPS .text / user-space load base for this binary

# --- Signature of the buggy store block in album_cover_parser -----------------
#   lw   v1, 8(s3)        0800638e
#   lw   v0, 0x538(s0)    3805028e
#   move a0, zero         25200000
#   sw   v1, 8(v0)        080043ac   <-- redirected (offset 12 in signature)
#   lw   v1, 0x10(s3)     1000638e   <-- becomes the j's delay slot; kept intact
SIG          = bytes.fromhex('0800638e' '3805028e' '25200000' '080043ac' '1000638e')
STORE_OFF    = 12          # offset of the 'sw' within SIG
REJOIN_OFF   = 20          # offset of the rejoin instruction (0x...284) within SIG

# Validation hook (28 bytes). Compares image pointer result[2] with file size result[1].
# On a failed decode, both fields hold the file size; we zero the pointer.
#   lw    t0, 4(s3)        0400688e   -> load result[1] size
#   lw    t1, 8(s3)        0800698e   -> load result[2] pointer / size
#   xor   t0, t0, t1       26400901   -> t0 = t0 ^ t1 (0 if equal)
#   movz  t1, zero, t0     0a480800   -> if equal (failed decode), t1 = 0
#   sw    t1, 8(v0)        080049ac   -> item+8 = sanitized value
#   j     <rejoin>         (computed)
#   nop                    00000000
HOOK_HEAD = bytes.fromhex('0400688e' '0800698e' '26400901' '0a480800' '080049ac')
HOOK_LEN  = len(HOOK_HEAD) + 8   # + j + nop = 28 bytes


def j_instr(target_va):
    return struct.pack('<I', 0x08000000 | ((target_va >> 2) & 0x03FFFFFF))


def is_jump_word(b4):
    # MIPS j/jal have opcode 2/3 -> top opcode bits 00001x; the high LE byte lands in 0x08..0x0F
    return len(b4) == 4 and 0x08 <= b4[3] <= 0x0F


JR_RA = b'\x08\x00\xe0\x03'   # 'jr ra' (LE)

def _is_ctrl_transfer(w):
    # jr ra, or j / jal (opcode 2/3 -> high LE byte 0x08..0x0f)
    return w == JR_RA or (len(w) == 4 and 0x08 <= w[3] <= 0x0f)

def find_safe_hook_slot(data, hooklen, lo=0xc3c0, hi=0x36bb60):
    """Return a file offset where the hook can be placed SAFELY, i.e. inside a zero run
    that begins immediately after an unconditional control transfer (jr ra / j). The run's
    first word is that transfer's delay slot (left as nop); the hook starts at run_start+4
    so its first instruction is never executed as a delay slot and is never reached by
    fall-through (the function already returned/jumped). Needs run length >= hooklen+4.

    This is the fix for the v1 boot loop: the previous version placed the hook's first
    instruction (a faulting `lw`) directly in a `jr ra` delay slot.
    """
    i = lo
    while i < hi - 4:
        if data[i:i+4] == b'\x00\x00\x00\x00':
            j = i
            while j < hi and data[j:j+4] == b'\x00\x00\x00\x00':
                j += 4
            if (j - i) - 4 >= hooklen and _is_ctrl_transfer(data[i-4:i]):
                return i + 4          # skip the delay-slot word
            i = j
        else:
            i += 4
    return -1


def patch(path):
    print(f"[*] Opening: {path}")
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    # Idempotency: signature with the 'sw' already replaced by a jump?
    head, tail = SIG[:STORE_OFF], SIG[REJOIN_OFF-4:]  # prefix + the kept lw v1,0x10(s3)
    start = 0
    while True:
        k = data.find(head, start)
        if k < 0:
            break
        store_word = data[k+STORE_OFF:k+STORE_OFF+4]
        if data[k+REJOIN_OFF-4:k+REJOIN_OFF] == tail and is_jump_word(store_word):
            print("[+] Already patched (store at sig+12 is a jump). Nothing to do.")
            return True
        start = k + 4

    # Locate the exact buggy store
    matches = [i for i in range(0, len(data)-len(SIG), 4) if data[i:i+len(SIG)] == SIG]
    if len(matches) == 0:
        print("[!] Store signature not found. Binary version unsupported by this patch.")
        return False
    if len(matches) > 1:
        print(f"[!] Signature matched {len(matches)} times (expected 1) -- aborting for safety.")
        return False
    sig_off = matches[0]
    store_off  = sig_off + STORE_OFF
    rejoin_va  = TEXT_BASE + sig_off + REJOIN_OFF
    print(f"[+] Found store at file offset {hex(store_off)} (VA {hex(TEXT_BASE+store_off)})")
    print(f"[+] Rejoin VA: {hex(rejoin_va)}")

    # Find a SAFE zero slot for the hook: inside a post-(jr ra/j) dead zone, clear of any
    # delay-slot position. (won't collide with other hooks, since theirs are no longer zero)
    pad_off = find_safe_hook_slot(data, HOOK_LEN)
    if pad_off < 0:
        print("[!] No safe .text padding slot found for the hook.")
        return False
    hook_va = TEXT_BASE + pad_off
    print(f"[+] Hook placed at file offset {hex(pad_off)} (VA {hex(hook_va)}) -- post-return dead zone")

    # Build hook = head + j(rejoin) + nop
    hook = bytearray(HOOK_HEAD)
    hook += j_instr(rejoin_va)
    hook += b'\x00\x00\x00\x00'
    assert len(hook) == HOOK_LEN

    # Apply: write hook, redirect the store to the hook
    data[pad_off:pad_off+HOOK_LEN] = hook
    data[store_off:store_off+4]    = j_instr(hook_va)

    # Backup + write
    bak = path + ".prepatch.bak"
    if os.path.exists(bak):
        os.remove(bak)
    os.rename(path, bak)
    with open(path, 'wb') as f:
        f.write(data)
        
    # Ensure the patched binary is executable
    os.chmod(path, 0o755)
    print(f"[*] Set executable permissions on: {path}")
    
    # Clear macOS extended attributes to avoid squashfs permissions issues
    if sys.platform == 'darwin':
        import subprocess
        subprocess.run(['xattr', '-c', path], capture_output=True)
        print(f"[*] Cleared macOS extended attributes on: {path}")
        
    print(f"[+] Patched. Backup: {bak}")
    print("[+] Done -- album-art SIGSEGV fixed at the source (covers that fail to decode now render blank).")
    return True


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 patch_player_source_fix.py <path_to_hiby_player>")
        sys.exit(1)
    sys.exit(0 if patch(sys.argv[1]) else 1)
