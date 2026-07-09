#!/usr/bin/env python3
import struct
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: patch_bt_input.py <hiby_player_binary>")
        sys.exit(1)

    target_file = sys.argv[1]
    with open(target_file, 'rb') as f:
        data = bytearray(f.read())

    def find_string(s):
        idx = data.find(s + b'\x00')
        if idx != -1:
            return idx + 0x400000
        return None

    vg_addr = find_string(b'vg_bt_input_hiby')
    vol_addr = find_string(b'volume_sync')
    bt_in_addr = find_string(b'bt_input')

    if not all([vg_addr, vol_addr, bt_in_addr]):
        print("Error: Could not find required strings in the binary. Are you using an unmodified R1 hiby_player?")
        sys.exit(1)

    print(f"[+] Found vg_bt_input_hiby at {hex(vg_addr)}")
    print(f"[+] Found volume_sync at {hex(vol_addr)}")
    print(f"[+] Found bt_input at {hex(bt_in_addr)}")

    # 1. Patch the blocklist (FUN_004f7220)
    # Search for addiu s1, s1, LOW(vg_addr) -> 0x2631xxxx
    target_addiu_s1 = 0x26310000 | (vg_addr & 0xffff)
    target_bytes_s1 = struct.pack('<I', target_addiu_s1)
    
    blocklist_idx = data.find(target_bytes_s1)
    if blocklist_idx == -1:
        # Check if already patched to +1
        target_addiu_s1_patched = 0x26310000 | ((vg_addr + 1) & 0xffff)
        if data.find(struct.pack('<I', target_addiu_s1_patched)) != -1:
            print("[*] Blocklist already bypassed.")
        else:
            print("Error: Could not find blocklist instruction. Is the binary already heavily modified?")
            sys.exit(1)
    else:
        print(f"[+] Found blocklist instruction at file offset {hex(blocklist_idx)}. Bypassing blocklist...")
        new_addiu_s1 = 0x26310000 | ((vg_addr + 1) & 0xffff)
        data[blocklist_idx:blocklist_idx+4] = struct.pack('<I', new_addiu_s1)

    # 2. Patch the menu builder
    # Search for addiu a1, a1, LOW(vol_addr) -> 0x24a5xxxx
    target_addiu_a1 = 0x24a50000 | (vol_addr & 0xffff)
    target_bytes_a1 = struct.pack('<I', target_addiu_a1)
    
    # Only pick the first occurrence (stock code)
    menu_idx = data.find(target_bytes_a1)
    if menu_idx == -1:
        print("Error: Could not find menu builder instruction.")
        sys.exit(1)
        
    menu_start_idx = menu_idx - 12
    # Verify the sequence:
    # lui a1, HIGH(vol_addr)
    # move a0, s2
    # jal add_item
    # addiu a1, a1, LOW(vol_addr)
    
    lui_instr, = struct.unpack('<I', data[menu_start_idx:menu_start_idx+4])
    move_instr, = struct.unpack('<I', data[menu_start_idx+4:menu_start_idx+8])
    jal_instr, = struct.unpack('<I', data[menu_start_idx+8:menu_start_idx+12])
    
    if (lui_instr >> 16) != 0x3c05 or move_instr != 0x02402025 or (jal_instr >> 26) != 0x03:
        print("Error: Menu builder sequence mismatch. Has it already been patched with a code cave?")
        sys.exit(1)
        
    add_item_func = (jal_instr & 0x03ffffff) << 2
    print(f"[+] Found menu builder sequence at {hex(menu_start_idx)}. UI add_item func: {hex(add_item_func)}")
    
    # 3. Find code cave (40 bytes of \x00)
    cave_size = 40
    zeros = b'\x00' * cave_size
    # Search in executable regions (.text or .rodata, starting around 0x300000)
    cave_idx = data.find(zeros, 0x300000)
    if cave_idx == -1:
        print("Error: Could not find code cave of 40 zero bytes.")
        sys.exit(1)

    # MIPS requires 4-byte aligned instructions
    misalign = cave_idx % 4
    if misalign:
        cave_idx += 4 - misalign

    cave_vaddr = cave_idx + 0x400000
    print(f"[+] Found code cave at file offset {hex(cave_idx)} (vaddr {hex(cave_vaddr)})")
    
    # 4. Generate payload (Code Cave)
    # We will execute the original volume_sync addition, then add our new bt_input, then jump back.
    def get_high_low(addr):
        low = addr & 0xffff
        high = addr >> 16
        if low >= 0x8000:
            high += 1
        return high, low

    vol_high, vol_low = get_high_low(vol_addr)
    bt_high, bt_low = get_high_low(bt_in_addr)
    
    payload = struct.pack('<I', 0x3c050000 | vol_high) # lui a1, vol_high
    payload += struct.pack('<I', 0x02402025)           # move a0, s2
    payload += struct.pack('<I', jal_instr)            # jal add_item_func
    payload += struct.pack('<I', 0x24a50000 | vol_low) # addiu a1, a1, vol_low
    
    payload += struct.pack('<I', 0x3c050000 | bt_high) # lui a1, bt_high
    payload += struct.pack('<I', 0x02402025)           # move a0, s2
    payload += struct.pack('<I', jal_instr)            # jal add_item_func
    payload += struct.pack('<I', 0x24a50000 | bt_low)  # addiu a1, a1, bt_low
    
    ret_vaddr = menu_start_idx + 0x400000 + 16
    payload += struct.pack('<I', 0x08000000 | (ret_vaddr >> 2)) # j return_addr
    payload += struct.pack('<I', 0x00000000)                    # nop
    
    assert len(payload) == 40
    data[cave_idx:cave_idx+40] = payload
    
    # 5. Overwrite original menu sequence with jump to code cave
    j_cave = 0x08000000 | (cave_vaddr >> 2)
    overwrite = struct.pack('<I', j_cave) + b'\x00\x00\x00\x00' * 3
    data[menu_start_idx:menu_start_idx+16] = overwrite
    print(f"[+] Injected code cave jump at file offset {hex(menu_start_idx)}.")
    
    with open(target_file, 'wb') as f:
        f.write(data)
        
    print("[+] Patch applied successfully!")

if __name__ == '__main__':
    main()
