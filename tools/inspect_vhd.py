#!/usr/bin/env python3
"""tools/inspect_vhd.py -- Inspect MicroNT.vhd structure and report issues."""
import struct, sys, os

def main():
    if len(sys.argv) < 2:
        print("Usage: inspect_vhd.py <MicroNT.vhd>"); sys.exit(1)

    data = open(sys.argv[1], 'rb').read()
    S = 512
    OK = True

    def ok(msg):  print(f"  [OK]   {msg}")
    def err(msg): print(f"  [ERR]  {msg}"); nonlocal OK; OK = False
    def info(msg): print(f"  [INFO] {msg}")

    # ── VHD Footer ─────────────────────────────────────────────
    print("\n=== VHD Footer (last 512 bytes) ===")
    footer = data[-S:]
    cookie = footer[0:8]
    disk_type = struct.unpack_from('>I', footer, 60)[0]
    cur_size  = struct.unpack_from('>Q', footer, 40)[0]
    orig_size = struct.unpack_from('>Q', footer, 48)[0]

    if cookie == b'conectix': ok(f"Cookie: {cookie}")
    else: err(f"Bad cookie: {cookie}")

    if disk_type == 2: ok(f"Disk type: 2 (Fixed)")
    else: err(f"Disk type: {disk_type} (expected 2=Fixed)")

    info(f"Current size : {cur_size:,} bytes ({cur_size//1048576} MB)")
    info(f"Original size: {orig_size:,} bytes")
    info(f"File size    : {len(data):,} bytes")
    info(f"Raw disk size: {len(data)-512:,} bytes")

    # Checksum validation
    buf = bytearray(footer)
    stored_csum = struct.unpack_from('>I', buf, 64)[0]
    struct.pack_into('>I', buf, 64, 0)
    calc = (~sum(buf)) & 0xFFFFFFFF
    if calc == stored_csum: ok(f"Footer checksum: 0x{stored_csum:08X}")
    else: err(f"Footer checksum: stored=0x{stored_csum:08X} calc=0x{calc:08X}")

    # ── MBR ────────────────────────────────────────────────────
    print("\n=== MBR (sector 0) ===")
    mbr = data[0:S]
    if mbr[510:512] == b'\x55\xAA': ok("MBR signature 0x55AA")
    else: err(f"Bad MBR sig: {mbr[510:512].hex()}")
    ptype = mbr[450]
    if ptype == 0xEE: ok(f"Partition type: 0xEE (GPT Protective)")
    else: err(f"Partition type: 0x{ptype:02X} (expected 0xEE)")

    # ── GPT Primary ────────────────────────────────────────────
    print("\n=== GPT Primary Header (sector 1) ===")
    gpt = data[S:2*S]
    sig = gpt[0:8]
    if sig == b'EFI PART': ok(f"GPT signature: {sig}")
    else: err(f"Bad GPT sig: {sig}")

    my_lba   = struct.unpack_from('<Q', gpt, 24)[0]
    alt_lba  = struct.unpack_from('<Q', gpt, 32)[0]
    first_u  = struct.unpack_from('<Q', gpt, 40)[0]
    last_u   = struct.unpack_from('<Q', gpt, 48)[0]
    pe_lba   = struct.unpack_from('<Q', gpt, 72)[0]
    n_parts  = struct.unpack_from('<I', gpt, 80)[0]
    pe_size  = struct.unpack_from('<I', gpt, 84)[0]

    info(f"my_lba={my_lba} alt_lba={alt_lba} first_usable={first_u} last_usable={last_u}")
    info(f"pe_lba={pe_lba} n_parts={n_parts} pe_size={pe_size}")

    if my_lba == 1:  ok("my_lba=1")
    else: err(f"my_lba={my_lba} (expected 1)")

    # GPT header CRC
    h = bytearray(gpt[:92])
    stored_hcrc = struct.unpack_from('<I', h, 16)[0]
    struct.pack_into('<I', h, 16, 0)
    import binascii
    calc_hcrc = binascii.crc32(bytes(h)) & 0xFFFFFFFF
    if calc_hcrc == stored_hcrc: ok(f"GPT header CRC: 0x{stored_hcrc:08X}")
    else: err(f"GPT header CRC: stored=0x{stored_hcrc:08X} calc=0x{calc_hcrc:08X}")

    # ── Partition Entry ─────────────────────────────────────────
    print("\n=== GPT Partition Entry 0 ===")
    pe = data[2*S : 2*S + pe_size]
    type_guid = pe[0:16]
    part_start = struct.unpack_from('<Q', pe, 32)[0]
    part_end   = struct.unpack_from('<Q', pe, 40)[0]
    name = pe[56:128].decode('utf-16-le', errors='replace').rstrip('\x00')

    ESP_GUID = bytes([0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,
                      0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B])
    if type_guid == ESP_GUID: ok(f"Type GUID: EFI System Partition")
    else: err(f"Type GUID: {type_guid.hex()} (expected ESP GUID)")

    info(f"Start LBA: {part_start}  End LBA: {part_end}  Size: {(part_end-part_start+1)*S//1024} KB")
    info(f"Name: {name!r}")

    # ── FAT at ESP start ───────────────────────────────────────
    print(f"\n=== FAT Filesystem (starts at LBA {part_start}) ===")
    esp_off = part_start * S
    if esp_off >= len(data): err(f"ESP offset {esp_off} beyond file size"); return

    bs = data[esp_off : esp_off + S]
    if bs[510:512] == b'\x55\xAA': ok("Boot sector signature 0x55AA")
    else: err(f"Bad boot sector sig: {bs[510:512].hex()}")

    bps = struct.unpack_from('<H', bs, 11)[0]
    spc = bs[13]
    res = struct.unpack_from('<H', bs, 14)[0]
    nf  = bs[16]
    nre = struct.unpack_from('<H', bs, 17)[0]
    tot16 = struct.unpack_from('<H', bs, 19)[0]
    fat16 = struct.unpack_from('<H', bs, 22)[0]
    tot32 = struct.unpack_from('<I', bs, 32)[0]
    fat32 = struct.unpack_from('<I', bs, 36)[0]
    fat_type_16 = bs[54:62]
    fat_type_32 = bs[82:90] if len(bs) > 90 else b''

    info(f"Bytes/sector={bps} Sectors/cluster={spc} Reserved={res} NumFATs={nf}")
    info(f"RootEntries={nre} TotSec16={tot16} FAT16sz={fat16} TotSec32={tot32} FAT32sz={fat32}")

    if bps == 512: ok("BPS=512")
    else: err(f"BPS={bps} (expected 512)")

    # Determine FAT type
    if fat32 > 0:
        # FAT32
        root_cl = struct.unpack_from('<I', bs, 44)[0]
        fat_type_str = bs[82:90]
        info(f"FAT32: root_cluster={root_cl} type_str={fat_type_str!r}")
        if b'FAT32' in fat_type_str: ok("FAT type string: FAT32")
        else: err(f"FAT type string (FAT32 offset): {fat_type_str!r}")
        # Calculate cluster count
        data_start = res + nf*fat32
        tot = tot32 or tot16
        nc = (tot - data_start) // spc
        info(f"Cluster count: {nc}")
    else:
        # FAT12/16
        tot = tot32 or tot16
        data_start = res + nf*fat16 + (nre*32+511)//512
        nc = (tot - data_start) // spc
        fat_type_str = bs[54:62]
        info(f"FAT16/12: data_start={data_start} cluster_count={nc}")
        info(f"FAT type string (offset 54): {fat_type_str!r}")
        if nc >= 4085 and nc < 65525:
            ok(f"Cluster count {nc} is in FAT16 range (4085-65524)")
        elif nc < 4085:
            err(f"Cluster count {nc} < 4085 -- filesystem will be treated as FAT12!")
        if b'FAT16' in fat_type_str: ok("FAT type string: FAT16")
        else: err(f"FAT type string: {fat_type_str!r}")

    # ── File presence check ────────────────────────────────────
    print("\n=== File Search in FAT ===")
    # Search for 'BOOTX64' in the FAT image (simple byte scan)
    fat_raw = data[esp_off:]
    bootx64_pos = fat_raw.find(b'BOOTX64')
    micront_pos = fat_raw.find(b'MICRONT')
    mz_pos      = fat_raw.find(b'MZ\x90') or fat_raw.find(b'MZ\x00')

    if bootx64_pos > 0:
        ok(f"'BOOTX64' found at FAT offset {bootx64_pos}")
    else:
        err("'BOOTX64' NOT found in FAT image")

    if micront_pos > 0:
        ok(f"'MICRONT' found at FAT offset {micront_pos}")
    else:
        err("'MICRONT' NOT found in FAT image")

    mz_pos2 = fat_raw.find(b'MZ')
    if mz_pos2 > 0:
        ok(f"MZ header found at FAT offset {mz_pos2}")
    else:
        err("MZ header NOT found in FAT image (BOOTX64.EFI data missing)")

    # ── Summary ────────────────────────────────────────────────
    print(f"\n=== Summary ===")
    if OK:
        print("  ALL CHECKS PASSED - VHD structure looks valid")
    else:
        print("  ISSUES FOUND - see [ERR] lines above")

if __name__ == '__main__':
    main()
