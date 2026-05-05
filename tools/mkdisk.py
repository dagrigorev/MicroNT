#!/usr/bin/env python3
"""
tools/mkdisk.py -- MicroNT UEFI-bootable VHD builder (pure Python 3)

Disk layout:
  Sector 0       MBR  (partition type 0xEF = EFI System, LBA 2048)
  Sectors 1-2047 zeros (gap between MBR and partition)
  Sectors 2048+  FAT32 EFI System Partition:
                   /EFI/BOOT/BOOTX64.EFI
                   /boot/MICRONT.ELF
  [end]          VHD fixed-disk footer (512 bytes appended)

Why MBR + type 0xEF?
  VirtualBox UEFI (OVMF) scans MBR partitions for type 0xEF to build
  a boot entry whose device path INCLUDES the partition component:
    Sata(0x0,0xFFFF,0x0)/HD(1,MBR,0x12345678,0x800,0xF800)
  Then PartitionDxe creates a child block-IO handle for that partition,
  FatDxe installs SimpleFileSystem on it, and BDS finds BOOTX64.EFI.
  Without 0xEF, UEFI only creates a raw-disk entry, FatDxe installs
  on the wrong handle, and BDS returns "Not Found".

Usage:
  python tools/mkdisk.py <BOOTX64.EFI> <micront.elf> <output.vhd>
"""
import struct, sys, os, time, calendar
import uuid as _uuid

S = 512

MBR_SIG      = 0x4E544D52   # disk signature stored at MBR bytes 440-443
PART_START   = 2048          # partition LBA (1 MB alignment)
PART_TYPE    = 0xEF          # EFI System Partition

# ====================================================================
# VHD footer
# ====================================================================
def _chs(tot):
    spt = 17
    cth = tot // spt
    heads = max(4, (cth + 1023) // 1024)
    if heads > 16 or cth >= heads * 1024:
        spt = 31; heads = 16; cth = tot // spt
    if cth >= heads * 1024:
        spt = 63; heads = 16; cth = tot // spt
    return cth // heads, heads, spt

def vhd_footer(disk_bytes):
    buf = bytearray(S)
    buf[0:8] = b'conectix'
    struct.pack_into('>I', buf,  8, 0x00000002)
    struct.pack_into('>I', buf, 12, 0x00010000)
    struct.pack_into('>Q', buf, 16, 0xFFFFFFFFFFFFFFFF)
    epoch = calendar.timegm((2000,1,1,0,0,0,0,0,0))
    struct.pack_into('>I', buf, 24, max(0, int(time.time()) - epoch))
    buf[28:32] = b'py  '
    struct.pack_into('>I', buf, 32, 0x00010000)
    buf[36:40] = b'Wi2k'
    struct.pack_into('>Q', buf, 40, disk_bytes)
    struct.pack_into('>Q', buf, 48, disk_bytes)
    cyl, heads, spt = _chs(disk_bytes // S)
    struct.pack_into('>H', buf, 56, cyl); buf[58]=heads; buf[59]=spt
    struct.pack_into('>I', buf, 60, 2)
    buf[68:84] = _uuid.uuid4().bytes
    csum = (~sum(buf)) & 0xFFFFFFFF
    struct.pack_into('>I', buf, 64, csum)
    return bytes(buf)

# ====================================================================
# MBR
# ====================================================================
def build_mbr(part_start, part_sectors):
    """MBR with one EFI System Partition (type 0xEF)."""
    mbr = bytearray(S)

    # Disk signature at offset 440 (required for HD() node matching)
    struct.pack_into('<I', mbr, 440, MBR_SIG)
    # mbr[444:446] = 0x0000  (reserved, must be zero)

    # Partition entry at offset 446 (16 bytes)
    # Status, CHS first (3 bytes), Type, CHS last (3 bytes), LBA start (4), LBA size (4)
    # Use 0xFE/0xFF/0xFF for CHS last (overflow sentinel) -- UEFI uses LBA only
    mbr[446] = 0x00          # status: not bootable
    mbr[447] = 0x00          # CHS first head (simplified)
    mbr[448] = 0x02          # CHS first sector (starts at 1, so sector 2 for head 0)
    mbr[449] = 0x00          # CHS first cylinder
    mbr[450] = PART_TYPE     # 0xEF = EFI System Partition
    mbr[451] = 0xFE          # CHS last head
    mbr[452] = 0xFF          # CHS last sector+cylinder high
    mbr[453] = 0xFF          # CHS last cylinder
    struct.pack_into('<I', mbr, 454, part_start)    # LBA start
    struct.pack_into('<I', mbr, 458, part_sectors)  # LBA size

    # Partition entries 2-4: all zeros (empty)

    mbr[510] = 0x55; mbr[511] = 0xAA
    return bytes(mbr)

# ====================================================================
# FAT32 partition
# ====================================================================
def build_fat32_part(bootx64: bytes, kernel: bytes,
                     part_sectors: int, hidden_sectors: int,
                     extra_files=None) -> bytes:
    """
    extra_files: list of (filename_str, data_bytes) to add to /boot/
    e.g. [("hello3.exe", hello3_bytes)]
    """
    if extra_files is None: extra_files = []
    """
    Build a FAT32 filesystem for a partition of part_sectors * 512 bytes.
    hidden_sectors = LBA offset of this partition within the disk
                     (stored in BPB_HiddSec so UEFI can verify LBA consistency).
    """
    SPC = 1       # sectors per cluster = 512 bytes
    # NOTE: FAT32 requires >= 65525 clusters.
    # With SPC=1, a 64MB disk gives ~127K clusters (FAT32 valid).
    # With SPC=8, a 300MB disk would be needed -- too large for a boot disk.
    CSZ = SPC * S
    RES = 32      # reserved sectors (BPB + FSInfo + backup)
    NF  = 2       # number of FATs

    # Compute FAT size
    fat_sec = 1
    for _ in range(8):
        ds = RES + NF * fat_sec
        nc = (part_sectors - ds) // SPC
        fat_sec = max(1, (nc * 4 + S - 1) // S)
    ds = RES + NF * fat_sec
    nc = (part_sectors - ds) // SPC

    # FAT type is determined by cluster count, NOT by BPB string.
    # FatDxe will treat <4085 as FAT12, <65525 as FAT16, >=65525 as FAT32.
    # We must be >=65525 for the BPB_FATSz32/BPB_RootClus fields to be read.
    assert nc >= 65525, (
        f"Only {nc} clusters -- filesystem would be treated as FAT16 by UEFI FatDxe! "
        f"Need >=65525 clusters. Increase DISK_MB or decrease SPC."
    )

    image = bytearray(part_sectors * S)

    # ---- BPB -------------------------------------------------------
    bs = bytearray(S)
    bs[0:3]   = b'\xEB\x58\x90'
    bs[3:11]  = b'MSWIN4.1'
    struct.pack_into('<H', bs, 11, S)
    bs[13]    = SPC
    struct.pack_into('<H', bs, 14, RES)
    bs[16]    = NF
    # RootEntCnt = 0, TotSec16 = 0
    bs[21]    = 0xF8          # fixed media
    # FATSz16 = 0 (FAT32)
    struct.pack_into('<H', bs, 24, 63)
    struct.pack_into('<H', bs, 26, 255)
    struct.pack_into('<I', bs, 28, hidden_sectors) # BPB_HiddSec
    struct.pack_into('<I', bs, 32, part_sectors)   # BPB_TotSec32
    struct.pack_into('<I', bs, 36, fat_sec)        # BPB_FATSz32
    # ExtFlags=0, FSVer=0
    struct.pack_into('<I', bs, 44, 2)              # BPB_RootClus
    struct.pack_into('<H', bs, 48, 1)              # BPB_FSInfo
    struct.pack_into('<H', bs, 50, 6)              # BPB_BkBootSec
    bs[64]    = 0x80          # BS_DrvNum
    bs[66]    = 0x29          # BS_BootSig
    struct.pack_into('<I', bs, 67, 0x4E544D52)
    bs[71:82] = b'MICRONT-EFI'
    bs[82:90] = b'FAT32   '
    bs[510]   = 0x55; bs[511] = 0xAA

    for sec in (0, 6):
        image[sec*S:(sec+1)*S] = bs

    # ---- FSInfo (sector 1) ----------------------------------------
    fsi = bytearray(S)
    struct.pack_into('<I', fsi,   0, 0x41615252)
    struct.pack_into('<I', fsi, 484, 0x61417272)
    struct.pack_into('<I', fsi, 488, 0xFFFFFFFF)
    struct.pack_into('<I', fsi, 492, 0xFFFFFFFF)
    struct.pack_into('<I', fsi, 508, 0xAA550000)
    image[1*S:2*S] = fsi

    # ---- FAT -------------------------------------------------------
    def ncl(sz): return max(1, (sz + CSZ - 1) // CSZ)

    CL_ROOT = 2; CL_EFI = 3; CL_EB = 4; CL_BOOT = 5; CL_BX = 6
    n_bx  = ncl(len(bootx64))
    CL_KRN = CL_BX + n_bx
    n_krn = ncl(len(kernel))

    # Extra files in /boot/: assign clusters sequentially after kernel
    extra_cl = []   # (name, first_cluster, data)
    cl_cur = CL_KRN + n_krn
    for xname, xdata in extra_files:
        extra_cl.append((xname, cl_cur, xdata))
        cl_cur += ncl(len(xdata))

    fat_buf = bytearray(fat_sec * S)

    def fat_set(cl, val):
        o = cl * 4
        if o + 4 <= len(fat_buf):
            struct.pack_into('<I', fat_buf, o, val & 0x0FFFFFFF)

    struct.pack_into('<I', fat_buf, 0, 0x0FFFFFF8)
    struct.pack_into('<I', fat_buf, 4, 0x0FFFFFFF)
    for cl in (CL_ROOT, CL_EFI, CL_EB, CL_BOOT):
        fat_set(cl, 0x0FFFFFFF)
    for i in range(CL_BX, CL_BX + n_bx - 1): fat_set(i, i+1)
    fat_set(CL_BX + n_bx - 1, 0x0FFFFFFF)
    for i in range(CL_KRN, CL_KRN + n_krn - 1): fat_set(i, i+1)
    fat_set(CL_KRN + n_krn - 1, 0x0FFFFFFF)
    for xname, xcl, xdata in extra_cl:
        nx = ncl(len(xdata))
        for i in range(xcl, xcl + nx - 1): fat_set(i, i+1)
        fat_set(xcl + nx - 1, 0x0FFFFFFF)

    f1 = RES * S; f2 = f1 + fat_sec * S
    image[f1:f1+len(fat_buf)] = fat_buf
    image[f2:f2+len(fat_buf)] = fat_buf

    # ---- Directories -----------------------------------------------
    def clo(cl): return (ds + (cl-2)*SPC) * S
    ADIR=0x10; AFILE=0x20

    def de(n8, e3, cl, sz, attr):
        e = bytearray(32)
        e[0:8]  = n8[:8].ljust(8, b' ')
        e[8:11] = e3[:3].ljust(3, b' ')
        e[11]   = attr
        struct.pack_into('<H', e, 20, (cl>>16)&0xFFFF)
        struct.pack_into('<H', e, 26, cl&0xFFFF)
        struct.pack_into('<I', e, 28, sz)
        return bytes(e)

    # Root dir (cluster 2)
    d = bytearray(CSZ)
    d[0:32]  = de(b'.', b'  ', CL_ROOT, 0, ADIR)
    d[32:64] = de(b'..', b' ', 0, 0, ADIR)
    d[64:96] = de(b'EFI', b'   ', CL_EFI, 0, ADIR)
    d[96:128]= de(b'BOOT', b'   ', CL_BOOT, 0, ADIR)
    image[clo(CL_ROOT):clo(CL_ROOT)+CSZ] = d

    # /EFI/ (cluster 3)
    d = bytearray(CSZ)
    d[0:32]  = de(b'.', b'  ', CL_EFI, 0, ADIR)
    d[32:64] = de(b'..', b' ', CL_ROOT, 0, ADIR)
    d[64:96] = de(b'BOOT', b'   ', CL_EB, 0, ADIR)
    image[clo(CL_EFI):clo(CL_EFI)+CSZ] = d

    # /EFI/BOOT/ (cluster 4)
    d = bytearray(CSZ)
    d[0:32]  = de(b'.', b'  ', CL_EB, 0, ADIR)
    d[32:64] = de(b'..', b' ', CL_EFI, 0, ADIR)
    d[64:96] = de(b'BOOTX64', b'EFI', CL_BX, len(bootx64), AFILE)
    image[clo(CL_EB):clo(CL_EB)+CSZ] = d

    # /boot/ (cluster 5)
    d = bytearray(CSZ)
    d[0:32]  = de(b'.', b'  ', CL_BOOT, 0, ADIR)
    d[32:64] = de(b'..', b' ', CL_ROOT, 0, ADIR)
    d[64:96] = de(b'MICRONT', b'ELF', CL_KRN, len(kernel), AFILE)
    slot = 96
    for xname, xcl, xdata in extra_cl:
        parts = xname.upper().split('.')
        nm8 = parts[0][:8].encode().ljust(8, b' ')
        ex3 = (parts[1][:3] if len(parts)>1 else '   ').encode().ljust(3, b' ')
        d[slot:slot+32] = de(nm8, ex3, xcl, len(xdata), AFILE)
        slot += 32
        if slot + 32 > CSZ: break   # directory full
    image[clo(CL_BOOT):clo(CL_BOOT)+CSZ] = d

    # File data
    o = clo(CL_BX); image[o:o+len(bootx64)] = bootx64
    o = clo(CL_KRN); image[o:o+len(kernel)] = kernel
    for xname, xcl, xdata in extra_cl:
        o = clo(xcl); image[o:o+len(xdata)] = xdata

    return bytes(image)

# ====================================================================
# PE32+ validation
# ====================================================================
def verify_efi(path, data):
    def fail(m): print(f'[ERROR] {path}: {m}'); sys.exit(1)
    if len(data)<64: fail('too small')
    if data[:2]!=b'MZ': fail('missing MZ')
    lfa=struct.unpack_from('<I',data,0x3C)[0]
    if lfa+96>len(data): fail('e_lfanew OOB')
    if data[lfa:lfa+4]!=b'PE\x00\x00': fail('bad PE sig')
    mach=struct.unpack_from('<H',data,lfa+4)[0]
    mag =struct.unpack_from('<H',data,lfa+24)[0]
    sub =struct.unpack_from('<H',data,lfa+92)[0]
    if mach!=0x8664: fail(f'machine 0x{mach:04X}!=AMD64')
    if mag !=0x020B: fail(f'magic 0x{mag:04X}!=PE32+')
    if sub !=10:     fail(f'subsystem {sub}!=10(EFI_APPLICATION)')
    print(f'  PE32+ OK    : AMD64 EFI_APPLICATION {len(data):,} bytes')

# ====================================================================
# Main
# ====================================================================
def main():
    if len(sys.argv)!=4:
        print('Usage: mkdisk.py <BOOTX64.EFI> <micront.elf> <output.vhd>')
        sys.exit(1)

    efi_path, elf_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    for p in (efi_path, elf_path):
        if not os.path.isfile(p): print(f'[ERROR] Not found: {p}'); sys.exit(1)

    efi_data = open(efi_path,'rb').read()
    elf_data = open(elf_path,'rb').read()

    print('[mkdisk] Building MicroNT.vhd (MBR+FAT32, type 0xEF)')
    verify_efi(efi_path, efi_data)
    print(f'  micront.elf : {len(elf_data):>10,} bytes')

    content_mb  = (len(efi_data)+len(elf_data)+1048575)//1048576
    DISK_MB     = max(64, content_mb+4)  # 64MB min -> >65525 clusters for valid FAT32
    DISK_SECTORS = DISK_MB * 2048
    DISK_BYTES   = DISK_SECTORS * S

    PART_SECTORS = DISK_SECTORS - PART_START

    print(f'  Disk size   : {DISK_MB} MB ({DISK_SECTORS} sectors)')
    print(f'  Partition   : LBA {PART_START}-{DISK_SECTORS-1} ({PART_SECTORS} sectors, type 0xEF)')

    disk = bytearray(DISK_BYTES)

    # MBR at sector 0
    disk[0:S] = build_mbr(PART_START, PART_SECTORS)

    # FAT32 partition starting at PART_START
    print('  Building FAT32 filesystem...')
    # Load extra boot files (*.exe) from the tools/ directory
    import os as _os
    script_dir = _os.path.dirname(_os.path.abspath(__file__))
    extra_files = []
    for fn in sorted(_os.listdir(script_dir)):
        if fn.lower().endswith('.exe'):
            fp = _os.path.join(script_dir, fn)
            data = open(fp, 'rb').read()
            extra_files.append((fn, data))
            print(f'  Extra file  : {fn} ({len(data):,} bytes)')
    fat = build_fat32_part(efi_data, elf_data, PART_SECTORS, PART_START,
                           extra_files=extra_files)
    assert len(fat) == PART_SECTORS * S
    disk[PART_START*S : PART_START*S + len(fat)] = fat

    # Sanity checks
    assert disk[510:512]==b'\x55\xAA', 'MBR sig'
    assert disk[450]==0xEF, 'partition type'
    assert b'BOOTX64' in disk
    assert b'FAT32   ' in disk

    with open(out_path,'wb') as f:
        f.write(disk)
        f.write(vhd_footer(DISK_BYTES))

    print(f'  VHD output  : {out_path}')
    print(f'  File size   : {DISK_BYTES+S:,} bytes')
    print('[mkdisk] Done.')

if __name__=='__main__':
    main()
