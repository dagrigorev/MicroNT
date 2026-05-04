#!/usr/bin/env python3
"""
tools/mkiso.py -- MicroNT UEFI-bootable ISO creator (pure Python 3)

Boot chain:
  1. UEFI reads El Torito catalog (sector 19)
  2. El Torito EFI entry -> LBA 25 (start of FAT12 ESP image)
  3. UEFI mounts the FAT12 image (FAT driver, always present in OVMF)
  4. Loads /EFI/BOOT/BOOTX64.EFI from the FAT image
  5. Bootloader reads /boot/micront.elf from same FAT image -> jumps to kernel

ISO layout (1 ISO sector = 2048 bytes):
  Sectors  0-15    System area (zeros)
  Sector   16      Primary Volume Descriptor  (ISO 9660)
  Sector   17      El Torito Boot Record VD
  Sector   18      VD Set Terminator
  Sector   19      El Torito Boot Catalog
  Sector   20      Path Table
  Sector   21      Root directory extent
  Sector   22      /EFI/ directory extent
  Sector   23      /EFI/BOOT/ directory extent
  Sector   24      /boot/ directory extent
  Sector   25+     FAT12 ESP image  <- El Torito LBA points here
  After ESP        micront.elf (also in FAT image; here for ISO 9660 completeness)

Usage:
  python tools/mkiso.py <BOOTX64.EFI> <micront.elf> <output.iso>
"""

import struct, sys, os

# ====================================================================
# Constants
# ====================================================================
ISO_SECTOR = 2048
FAT_SECTOR = 512          # FAT uses 512-byte sectors internally

# Fixed ISO 9660 sector assignments
LBA_PVD        = 16
LBA_ELTORITO   = 17
LBA_TERMINATOR = 18
LBA_BOOT_CAT   = 19
LBA_PATH_TABLE = 20
LBA_ROOT_DIR   = 21
LBA_EFI_DIR    = 22
LBA_EFI_BOOT   = 23
LBA_BOOT_DIR   = 24
LBA_ESP        = 25       # FAT12 image starts here (El Torito LBA)

DIR_SIZE = ISO_SECTOR     # each directory occupies 1 ISO sector

DATE7  = bytes([125, 1, 1, 0, 0, 0, 0])
DATE17 = b'20250101000000' + b'00' + b'\x00'

# ====================================================================
# Helpers
# ====================================================================
def b16(n):
    return struct.pack('<H', n) + struct.pack('>H', n)

def b32(n):
    return struct.pack('<I', n) + struct.pack('>I', n)

def pad_to(data: bytes, boundary: int) -> bytes:
    r = len(data) % boundary
    return data + (b'\x00' * (boundary - r) if r else b'')

def iso_sectors(size: int) -> int:
    return (size + ISO_SECTOR - 1) // ISO_SECTOR

def fat_sectors(size: int) -> int:
    return (size + FAT_SECTOR - 1) // FAT_SECTOR

# ====================================================================
# FAT12 ESP image builder
# ====================================================================
def build_fat12_esp(bootx64_data: bytes, kernel_data: bytes) -> bytes:
    """
    Build a minimal FAT12 filesystem image containing:
      /EFI/BOOT/BOOTX64.EFI
      /boot/MICRONT.ELF
    Returns the raw image bytes (multiple of ISO_SECTOR).
    """
    CLUSTER_SECTORS = 4          # 4 x 512 = 2048 bytes per cluster
    CLUSTER_SIZE = CLUSTER_SECTORS * FAT_SECTOR
    RESERVED     = 1             # one boot sector
    N_FATS       = 2
    FAT_SEC      = 1             # one 512-byte FAT sector per copy
    ROOT_ENTRIES = 64            # 64 x 32 = 2048 bytes = 4 sectors
    ROOT_SECTORS = (ROOT_ENTRIES * 32 + FAT_SECTOR - 1) // FAT_SECTOR

    data_start_sector = RESERVED + N_FATS * FAT_SEC + ROOT_SECTORS

    def n_clusters(size):
        return (size + CLUSTER_SIZE - 1) // CLUSTER_SIZE

    # Cluster allocation (FAT12 data starts at cluster 2)
    CL_EFI       = 2
    CL_EFIBOOT   = 3
    CL_BOOT      = 4
    CL_BOOTX64   = 5
    n_bootx64    = n_clusters(len(bootx64_data))
    CL_KERNEL    = CL_BOOTX64 + n_bootx64
    n_kernel     = n_clusters(len(kernel_data))
    last_cluster = CL_KERNEL + n_kernel - 1

    total_fat_sectors = data_start_sector + (last_cluster - 2 + 1) * CLUSTER_SECTORS
    # round up to multiple of 4 FAT sectors (= 1 ISO sector)
    total_fat_sectors = ((total_fat_sectors + 3) // 4) * 4

    image = bytearray(total_fat_sectors * FAT_SECTOR)

    # ---- Boot Sector ------------------------------------------------
    bs = bytearray(FAT_SECTOR)
    bs[0:3]   = b'\xEB\x3C\x90'
    bs[3:11]  = b'MSWIN4.1'
    struct.pack_into('<H', bs, 11, FAT_SECTOR)
    bs[13]    = CLUSTER_SECTORS
    struct.pack_into('<H', bs, 14, RESERVED)
    bs[16]    = N_FATS
    struct.pack_into('<H', bs, 17, ROOT_ENTRIES)
    struct.pack_into('<H', bs, 19, total_fat_sectors)
    bs[21]    = 0xF8              # fixed media
    struct.pack_into('<H', bs, 22, FAT_SEC)
    struct.pack_into('<H', bs, 24, 1)    # sectors/track
    struct.pack_into('<H', bs, 26, 1)    # heads
    struct.pack_into('<I', bs, 28, 0)    # hidden
    struct.pack_into('<I', bs, 32, 0)
    bs[36]    = 0x80
    bs[38]    = 0x29
    struct.pack_into('<I', bs, 39, 0xDEADBEEF)
    bs[43:54] = b'MICRONT-ESP'
    bs[54:62] = b'FAT12   '
    bs[510]   = 0x55; bs[511] = 0xAA
    image[0:FAT_SECTOR] = bs

    # ---- FAT --------------------------------------------------------
    fat = {}  # cluster -> next cluster value (0xFFF = end of chain)

    def alloc_chain(start, count):
        for i in range(count - 1):
            fat[start + i] = start + i + 1
        fat[start + count - 1] = 0xFFF

    fat[0] = 0xFF8  # media descriptor
    fat[1] = 0xFFF  # reserved
    fat[CL_EFI]     = 0xFFF
    fat[CL_EFIBOOT] = 0xFFF
    fat[CL_BOOT]    = 0xFFF
    alloc_chain(CL_BOOTX64, n_bootx64)
    alloc_chain(CL_KERNEL,  n_kernel)

    # Encode FAT12
    max_cl = last_cluster + 1
    fat_buf = bytearray(FAT_SEC * FAT_SECTOR)
    for i in range(max_cl + 1):
        val = fat.get(i, 0) & 0xFFF
        byte_off = (i * 3) // 2
        if byte_off + 1 >= len(fat_buf):
            break
        if i % 2 == 0:
            fat_buf[byte_off]     = val & 0xFF
            fat_buf[byte_off + 1] = (fat_buf[byte_off + 1] & 0xF0) | ((val >> 8) & 0x0F)
        else:
            fat_buf[byte_off]     = (fat_buf[byte_off] & 0x0F) | ((val & 0x0F) << 4)
            fat_buf[byte_off + 1] = (val >> 4) & 0xFF

    fat1_off = RESERVED * FAT_SECTOR
    fat2_off = fat1_off + FAT_SEC * FAT_SECTOR
    image[fat1_off:fat1_off + len(fat_buf)] = fat_buf
    image[fat2_off:fat2_off + len(fat_buf)] = fat_buf

    # ---- Directory helpers ------------------------------------------
    def fat_dirent(name8: bytes, ext3: bytes, cluster: int,
                   size: int, attr: int) -> bytes:
        e = bytearray(32)
        e[0:8]  = name8.ljust(8)[:8]
        e[8:11] = ext3.ljust(3)[:3]
        e[11]   = attr
        struct.pack_into('<H', e, 26, cluster & 0xFFFF)
        struct.pack_into('<I', e, 28, size)
        return bytes(e)

    ATTR_DIR  = 0x10
    ATTR_FILE = 0x20

    def dot(cl):
        return fat_dirent(b'.', b'  ', cl, 0, ATTR_DIR)

    def dotdot(parent_cl):
        return fat_dirent(b'..', b'  ', parent_cl, 0, ATTR_DIR)

    def subdir(name, cl):
        return fat_dirent(name.encode(), b'   ', cl, 0, ATTR_DIR)

    def filent(name, ext, cl, sz):
        return fat_dirent(name.encode(), ext.encode(), cl, sz, ATTR_FILE)

    # Root directory
    root_off = (RESERVED + N_FATS * FAT_SEC) * FAT_SECTOR
    root = bytearray(ROOT_ENTRIES * 32)
    root[0:32]  = subdir('EFI', CL_EFI)
    root[32:64] = subdir('BOOT', CL_BOOT)
    image[root_off:root_off + len(root)] = root

    def cluster_off(cl):
        return (data_start_sector + (cl - 2) * CLUSTER_SECTORS) * FAT_SECTOR

    # /EFI/ directory
    efi_dir = bytearray(CLUSTER_SIZE)
    efi_dir[0:32]  = dot(CL_EFI)
    efi_dir[32:64] = dotdot(0)
    efi_dir[64:96] = subdir('BOOT', CL_EFIBOOT)
    off = cluster_off(CL_EFI)
    image[off:off + CLUSTER_SIZE] = efi_dir

    # /EFI/BOOT/ directory
    efi_boot_dir = bytearray(CLUSTER_SIZE)
    efi_boot_dir[0:32]  = dot(CL_EFIBOOT)
    efi_boot_dir[32:64] = dotdot(CL_EFI)
    efi_boot_dir[64:96] = filent('BOOTX64', 'EFI', CL_BOOTX64, len(bootx64_data))
    off = cluster_off(CL_EFIBOOT)
    image[off:off + CLUSTER_SIZE] = efi_boot_dir

    # /boot/ directory
    boot_dir = bytearray(CLUSTER_SIZE)
    boot_dir[0:32]  = dot(CL_BOOT)
    boot_dir[32:64] = dotdot(0)
    boot_dir[64:96] = filent('MICRONT', 'ELF', CL_KERNEL, len(kernel_data))
    off = cluster_off(CL_BOOT)
    image[off:off + CLUSTER_SIZE] = boot_dir

    # File data
    off = cluster_off(CL_BOOTX64)
    image[off:off + len(bootx64_data)] = bootx64_data

    off = cluster_off(CL_KERNEL)
    image[off:off + len(kernel_data)] = kernel_data

    # Pad to ISO sector boundary
    return bytes(pad_to(bytes(image), ISO_SECTOR))

# ====================================================================
# ISO 9660 directory record
# ====================================================================
def dirrec(name: bytes, lba: int, size: int, is_dir: bool) -> bytes:
    flags   = 0x02 if is_dir else 0x00
    pad_fi  = b'\x00' if len(name) % 2 == 0 else b''
    dr_len  = 33 + len(name) + len(pad_fi)
    return (
        struct.pack('BB', dr_len, 0) +
        b32(lba) + b32(size) +
        DATE7 +
        struct.pack('BBB', flags, 0, 0) +
        b16(1) +
        struct.pack('B', len(name)) +
        name + pad_fi
    )

def dir_extent(self_lba, parent_lba, children) -> bytes:
    data  = dirrec(b'\x00', self_lba,   DIR_SIZE, True)
    data += dirrec(b'\x01', parent_lba, DIR_SIZE, True)
    for name, lba, size, is_dir in children:
        data += dirrec(name, lba, size, is_dir)
    return pad_to(data, ISO_SECTOR)

# ====================================================================
# Path Table
# ====================================================================
def build_path_table(entries):
    raw = b''
    for name, lba, parent in entries:
        pad_name = b'\x00' if len(name) % 2 else b''
        raw += (
            struct.pack('BB', len(name), 0) +
            struct.pack('<I', lba) +
            struct.pack('<H', parent) +
            name + pad_name
        )
    return pad_to(raw, ISO_SECTOR), len(raw)

# ====================================================================
# El Torito Boot Catalog
# ====================================================================
def boot_catalog(esp_lba: int, esp_iso_sectors: int) -> bytes:
    """
    El Torito boot catalog pointing to FAT12 ESP image.
    esp_lba         -- ISO sector where FAT image starts
    esp_iso_sectors -- size of FAT image in ISO sectors

    OVMF's PartitionDxe reads the El Torito catalog, finds this EFI
    entry, creates a child partition starting at esp_lba, and mounts it
    with the FAT driver.  The FAT image contains /EFI/BOOT/BOOTX64.EFI.
    """
    # Validation Entry (32 bytes)
    val = bytearray(32)
    val[0]  = 0x01     # header ID
    val[1]  = 0xEF     # platform: EFI
    val[28] = 0x55
    val[29] = 0xAA
    sub = sum(val[i] | (val[i+1] << 8) for i in range(0, 32, 2)) & 0xFFFF
    ck  = (0x10000 - sub) & 0xFFFF
    val[30] = ck & 0xFF; val[31] = ck >> 8

    # Default/Initial Entry (32 bytes)
    init = bytearray(32)
    init[0] = 0x88    # bootable
    init[1] = 0x00    # no emulation
    # sector count: ESP size in 512-byte virtual sectors
    virt_sectors = esp_iso_sectors * (ISO_SECTOR // FAT_SECTOR)
    struct.pack_into('<H', init, 6, virt_sectors)
    struct.pack_into('<I', init, 8, esp_lba)   # LBA of FAT12 image

    # Section Header (32 bytes) -- required by OVMF to create EFI boot entry
    shdr = bytearray(32)
    shdr[0] = 0x91    # last section header
    shdr[1] = 0xEF    # platform: EFI
    struct.pack_into('<H', shdr, 2, 1)

    # Section Entry (32 bytes)
    sent = bytearray(32)
    sent[0] = 0x88
    sent[1] = 0x00
    struct.pack_into('<H', sent, 6, virt_sectors)
    struct.pack_into('<I', sent, 8, esp_lba)

    return pad_to(bytes(val) + bytes(init) + bytes(shdr) + bytes(sent), ISO_SECTOR)

# ====================================================================
# Primary Volume Descriptor
# ====================================================================
def build_pvd(total_sectors, root_lba, path_lba, path_size) -> bytes:
    buf = bytearray(ISO_SECTOR)
    buf[0]   = 0x01
    buf[1:6] = b'CD001'
    buf[6]   = 0x01
    buf[8:40]  = b'MICRONT' + b' ' * 25
    buf[40:72] = b'MICRONT' + b' ' * 25
    buf[80:88]   = b32(total_sectors)
    buf[120:124] = b16(1)
    buf[124:128] = b16(1)
    buf[128:132] = b16(ISO_SECTOR)
    buf[132:140] = b32(path_size)
    struct.pack_into('<I', buf, 140, path_lba)
    struct.pack_into('>I', buf, 148, path_lba)
    rr = dirrec(b'\x00', root_lba, DIR_SIZE, True)
    assert len(rr) == 34
    buf[156:190] = rr
    buf[190:318] = b' ' * 128
    buf[318:446] = b' ' * 128
    buf[446:574] = b' ' * 128
    buf[574:702] = b'MICRONT MKISO' + b' ' * 115
    buf[702:739] = b' ' * 37
    buf[739:775] = b' ' * 36
    buf[775:812] = b' ' * 37
    buf[812:829] = DATE17
    buf[829:846] = DATE17
    buf[846:863] = b'\x00' * 17
    buf[863:880] = DATE17
    buf[880]     = 0x01
    return bytes(buf)

def build_eltorito_vd(catalog_lba) -> bytes:
    buf = bytearray(ISO_SECTOR)
    buf[0]   = 0x00
    buf[1:6] = b'CD001'
    buf[6]   = 0x01
    spec = b'EL TORITO SPECIFICATION'
    buf[7:7+len(spec)] = spec
    struct.pack_into('<I', buf, 71, catalog_lba)
    return bytes(buf)

def build_terminator() -> bytes:
    buf = bytearray(ISO_SECTOR)
    buf[0]   = 0xFF
    buf[1:6] = b'CD001'
    buf[6]   = 0x01
    return bytes(buf)

# ====================================================================
# PE32+ validation
# ====================================================================
def verify_efi_pe(path: str, data: bytes):
    def fail(msg):
        print(f'[ERROR] {path}: {msg}'); sys.exit(1)
    if len(data) < 64:
        fail('too small')
    if data[:2] != b'MZ':
        fail(f'missing MZ magic (got {data[:2].hex()})')
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    if e_lfanew + 96 > len(data):
        fail('e_lfanew out of bounds')
    if data[e_lfanew:e_lfanew+4] != b'PE\x00\x00':
        fail('missing PE signature')
    machine   = struct.unpack_from('<H', data, e_lfanew + 4)[0]
    opt_magic = struct.unpack_from('<H', data, e_lfanew + 24)[0]
    subsystem = struct.unpack_from('<H', data, e_lfanew + 92)[0]
    if machine != 0x8664:
        fail(f'wrong machine 0x{machine:04X} (need AMD64=0x8664)')
    if opt_magic != 0x020B:
        fail(f'not PE32+ (magic=0x{opt_magic:04X})')
    if subsystem != 10:
        fail(f'wrong subsystem {subsystem} (need 10=EFI_APPLICATION)')
    print(f'  PE32+ OK    : AMD64 EFI_APPLICATION {len(data):,} bytes')

# ====================================================================
# Main
# ====================================================================
def main():
    if len(sys.argv) != 4:
        print('Usage: mkiso.py <BOOTX64.EFI> <micront.elf> <output.iso>')
        sys.exit(1)

    efi_path, elf_path, iso_path = sys.argv[1], sys.argv[2], sys.argv[3]
    for p in (efi_path, elf_path):
        if not os.path.isfile(p):
            print(f'[ERROR] Not found: {p}'); sys.exit(1)

    efi_data = open(efi_path, 'rb').read()
    elf_data = open(elf_path, 'rb').read()

    print('[mkiso] Building MicroNT.iso')
    verify_efi_pe(efi_path, efi_data)
    print(f'  micront.elf : {len(elf_data):>10,} bytes  ({iso_sectors(len(elf_data))} ISO sectors)')

    # Build FAT12 ESP image
    print('  Building FAT12 ESP image...')
    esp_data = build_fat12_esp(efi_data, elf_data)
    esp_iso_secs = len(esp_data) // ISO_SECTOR
    print(f'  FAT12 ESP   : {len(esp_data):>10,} bytes  ({esp_iso_secs} ISO sectors) at LBA {LBA_ESP}')

    # ISO 9660 file layout (after ESP)
    elf_lba = LBA_ESP + esp_iso_secs

    total_sectors = elf_lba + iso_sectors(len(elf_data))

    # Path table
    pt_entries = [
        (b'\x00', LBA_ROOT_DIR, 1),
        (b'BOOT',  LBA_BOOT_DIR, 1),
        (b'EFI',   LBA_EFI_DIR,  1),
        (b'BOOT',  LBA_EFI_BOOT, 3),
    ]
    pt_data, pt_size = build_path_table(pt_entries)

    # Directory extents (ISO 9660 side)
    root_extent = dir_extent(LBA_ROOT_DIR, LBA_ROOT_DIR, [
        (b'BOOT', LBA_BOOT_DIR, DIR_SIZE, True),
        (b'EFI',  LBA_EFI_DIR,  DIR_SIZE, True),
    ])
    efi_extent = dir_extent(LBA_EFI_DIR, LBA_ROOT_DIR, [
        (b'BOOT', LBA_EFI_BOOT, DIR_SIZE, True),
    ])
    efi_boot_extent = dir_extent(LBA_EFI_BOOT, LBA_EFI_DIR, [])
    boot_extent = dir_extent(LBA_BOOT_DIR, LBA_ROOT_DIR, [
        (b'MICRONT.ELF;1', elf_lba, len(elf_data), False),
    ])

    # Assemble
    image = bytearray()
    image += b'\x00' * (LBA_PVD * ISO_SECTOR)
    image += build_pvd(total_sectors, LBA_ROOT_DIR, LBA_PATH_TABLE, pt_size)
    image += build_eltorito_vd(LBA_BOOT_CAT)
    image += build_terminator()
    image += boot_catalog(LBA_ESP, esp_iso_secs)
    image += pt_data
    image += root_extent
    image += efi_extent
    image += efi_boot_extent
    image += boot_extent
    image += esp_data                    # FAT12 ESP  (El Torito LBA=25)
    image += pad_to(elf_data, ISO_SECTOR)  # micront.elf in ISO 9660

    assert len(image) % ISO_SECTOR == 0
    assert len(image) // ISO_SECTOR == total_sectors

    with open(iso_path, 'wb') as f:
        f.write(image)

    size_mb = len(image) / (1024*1024)
    print(f'  ISO         : {iso_path}')
    print(f'  Size        : {size_mb:.2f} MB  ({total_sectors} sectors)')
    print(f'  El Torito   : FAT12 ESP at LBA {LBA_ESP}')
    print(f'  micront.elf : ISO9660 at LBA {elf_lba}')
    print('[mkiso] Done.')

if __name__ == '__main__':
    main()
