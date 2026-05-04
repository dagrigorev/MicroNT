// bootloader.cpp - MicroNT UEFI Bootloader (verbose debug build)
// Entry: EfiMain (Windows/UEFI ABI)

#include "uefi.h"
#include "elf64.h"
#include "../../kernel/include/bootinfo.h"

// ============================================================
// Globals
// ============================================================
static EFI_SYSTEM_TABLE*  gST     = nullptr;
static EFI_BOOT_SERVICES* gBS     = nullptr;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* gConOut = nullptr;

// ============================================================
// Freestanding helpers
// ============================================================
static void* Memset(void* dst, int val, UINTN len) {
    auto* p = static_cast<UINT8*>(dst);
    while (len--) *p++ = (UINT8)val;
    return dst;
}
static void* Memcpy(void* dst, const void* src, UINTN len) {
    auto* d = static_cast<UINT8*>(dst);
    const auto* s = static_cast<const UINT8*>(src);
    while (len--) *d++ = *s++;
    return dst;
}

// ============================================================
// Console + Serial output
// (Serial COM1 = 0x3F8, already set up by UEFI firmware)
// We write to both UEFI ConOut and COM1 so output is captured
// in serial.log even when ConOut is unavailable.
// ============================================================
static void SerialPut(char c) {
    // Wait for transmit empty (LSR at COM1+5 = 0x3FD)
    // Ports > 255 must use dx register; "Nd" constraint handles this.
    for (int i = 0; i < 100000; i++) {
        UINT8 lsr;
        __asm__ volatile("inb %1, %0" : "=a"(lsr) : "Nd"((UINT16)0x3FD));
        if (lsr & 0x20) break;
    }
    __asm__ volatile("outb %0, %1" :: "a"((UINT8)c), "Nd"((UINT16)0x3F8));
}

static void SerialStr(const char* s) {
    while (*s) {
        if (*s == '\n') SerialPut('\r');
        SerialPut(*s++);
    }
}

static void ConPut(CHAR16 c) {
    if (!gConOut) return;
    CHAR16 buf[2] = { c, 0 };
    gConOut->OutputString(gConOut, buf);
}

static void Print(const char* msg) {
    SerialStr(msg);
    // NOTE: do NOT write to ConOut -- VirtualBox mirrors ConOut to COM1,
    // which would double every line in serial.log.
}

static void PrintHex64(UINT64 val) {
    char buf[19]; buf[0]='0'; buf[1]='x'; buf[18]=0;
    for (int i=17; i>=2; --i) {
        UINT8 n = val & 0xF;
        buf[i] = n<10 ? '0'+n : 'A'+n-10;
        val >>= 4;
    }
    Print(buf);
}

static void PrintDec(UINT64 val) {
    char buf[21]; UINT32 idx=20; buf[idx]=0;
    if (!val) { buf[--idx]='0'; }
    else { while(val){ buf[--idx]='0'+(val%10); val/=10; } }
    Print(buf+idx);
}

// ============================================================
// Logging macros
// ============================================================
#define LOG(msg)          Print("[BL] " msg "\n")
#define LOG_VAL(msg, val) do { Print("[BL] " msg); PrintHex64(val); Print("\n"); } while(0)
#define LOG_DEC(msg, val) do { Print("[BL] " msg); PrintDec(val); Print("\n"); } while(0)
#define LOG_OK(msg)       Print("[BL] [OK] " msg "\n")
#define LOG_ERR(msg)      Print("[BL] [ERR] " msg "\n")
#define TRACE(msg)        Print("[BL][TRACE] " msg "\n")

// ============================================================
// Panic
// ============================================================
[[noreturn]] static void Panic(const char* msg, EFI_STATUS st = 0) {
    Print("\n[BL] *** PANIC: ");
    Print(msg);
    if (st) { Print(" status="); PrintHex64(st); }
    Print("\n[BL] System halted.\n");
    for (;;) __asm__("cli; hlt");
}

static void Check(EFI_STATUS st, const char* ctx) {
    if (EFI_IS_ERROR(st)) {
        Print("[BL] [FAIL] "); Print(ctx); Print(" status=");
        PrintHex64(st); Print("\n");
        Panic(ctx, st);
    }
}

// ============================================================
// File loading
// ============================================================
static void LoadFile(EFI_HANDLE device, const CHAR16* path,
                     void** out_data, UINTN* out_size) {
    Print("[BL] LoadFile: ");
    // Print wchar path as ASCII approximation
    for (const CHAR16* p = path; *p; ++p) SerialPut((char)*p);
    Print("\n");

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = nullptr;
    EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    Check(gBS->HandleProtocol(device, &fsGuid, (void**)&fs),
          "HandleProtocol(SFS)");
    LOG_OK("SimpleFileSystem protocol acquired");

    EFI_FILE_PROTOCOL* root = nullptr;
    Check(fs->OpenVolume(fs, &root), "OpenVolume");
    LOG_OK("Volume opened");

    EFI_FILE_PROTOCOL* file = nullptr;
    EFI_STATUS st = root->Open(root, &file, const_cast<CHAR16*>(path),
                               EFI_FILE_MODE_READ, 0);
    if (EFI_IS_ERROR(st)) {
        Print("[BL] [ERR] Cannot open file, status="); PrintHex64(st);
        Print("\n");
        Panic("File open failed", st);
    }
    LOG_OK("File opened");

    EFI_FILE_INFO info;
    UINTN infoSz = sizeof(info);
    EFI_GUID figuid = EFI_FILE_INFO_GUID;
    Check(file->GetInfo(file, &figuid, &infoSz, &info), "GetInfo");

    UINTN fsize = (UINTN)info.FileSize;
    LOG_DEC("File size (bytes): ", fsize);

    void* buf = nullptr;
    Check(gBS->AllocatePool(EfiLoaderData, fsize, &buf), "AllocatePool(file)");
    LOG_VAL("Buffer allocated at: ", (UINT64)(UINTN)buf);

    UINTN rsize = fsize;
    Check(file->Read(file, &rsize, buf), "Read");
    LOG_DEC("Bytes read: ", rsize);
    file->Close(file); root->Close(root);

    *out_data = buf; *out_size = fsize;
    LOG_OK("File loaded successfully");
}

// ============================================================
// ELF64 loader
// ============================================================
static UINT64 LoadElf(void* elfData, UINTN elfSize,
                      UINT64* outBase, UINT64* outEnd) {
    TRACE("LoadElf: validating ELF header");
    auto* ehdr = static_cast<Elf64_Ehdr*>(elfData);

    if (ehdr->e_ident[EI_MAG0]!=ELFMAG0 || ehdr->e_ident[EI_MAG1]!=ELFMAG1 ||
        ehdr->e_ident[EI_MAG2]!=ELFMAG2 || ehdr->e_ident[EI_MAG3]!=ELFMAG3)
        Panic("Not an ELF file");
    if (ehdr->e_ident[EI_CLASS]!=ELFCLASS64)  Panic("Not ELF64");
    if (ehdr->e_machine!=EM_X86_64)           Panic("Not x86-64");

    LOG_VAL("ELF entry point (VA): ", ehdr->e_entry);
    LOG_DEC("Program header count: ", ehdr->e_phnum);

    UINT64 loadBase=~0ULL, loadEnd=0;
    auto* phdrs = reinterpret_cast<Elf64_Phdr*>(
        static_cast<UINT8*>(elfData)+ehdr->e_phoff);

    for (UINT16 i=0; i<ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type!=PT_LOAD) continue;
        Print("[BL]   LOAD seg "); PrintDec(i);
        Print("  paddr="); PrintHex64(phdrs[i].p_paddr);
        Print("  filesz="); PrintHex64(phdrs[i].p_filesz);
        Print("  memsz=");  PrintHex64(phdrs[i].p_memsz);
        Print("\n");
        if (phdrs[i].p_paddr < loadBase) loadBase = phdrs[i].p_paddr;
        UINT64 segEnd = phdrs[i].p_paddr + phdrs[i].p_memsz;
        if (segEnd > loadEnd) loadEnd = segEnd;
    }

    if (loadBase==~0ULL) Panic("No PT_LOAD segments");
    loadBase &= ~0xFFFULL;
    loadEnd   = (loadEnd+0xFFF)&~0xFFFULL;
    UINTN pages = (loadEnd-loadBase)/4096;

    LOG_VAL("Kernel load base (phys): ", loadBase);
    LOG_VAL("Kernel load end  (phys): ", loadEnd);
    LOG_DEC("Pages to allocate: ", pages);

    EFI_PHYSICAL_ADDRESS physBase = (EFI_PHYSICAL_ADDRESS)loadBase;
    EFI_STATUS st = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
                                       pages, &physBase);
    if (EFI_IS_ERROR(st)) {
        LOG_ERR("AllocateAddress failed, trying AnyPages");
        physBase = 0;
        Check(gBS->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                 pages, &physBase), "AllocatePages(any)");
    }
    LOG_VAL("Allocated at physical: ", physBase);

    Memset((void*)physBase, 0, pages*4096);
    TRACE("Zeroed kernel region");

    for (UINT16 i=0; i<ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type!=PT_LOAD || !phdrs[i].p_filesz) continue;
        UINT64 dst = physBase+(phdrs[i].p_paddr-loadBase);
        const void* src = (UINT8*)elfData+phdrs[i].p_offset;
        Memcpy((void*)dst, src, (UINTN)phdrs[i].p_filesz);
        Print("[BL]   Copied segment to "); PrintHex64(dst);
        Print(" ("); PrintDec(phdrs[i].p_filesz); Print(" bytes)\n");
    }
    LOG_OK("ELF segments loaded");

    *outBase = physBase;
    *outEnd  = physBase + pages*4096;
    UINT64 entry = physBase + (ehdr->e_entry - loadBase);
    LOG_VAL("Resolved entry point: ", entry);
    return entry;
}

// ============================================================
// Page tables: identity map 0-4 GB (2 MB huge pages)
// ============================================================
static UINT64 BuildPageTables() {
    TRACE("BuildPageTables: allocating 6 pages");
    EFI_PHYSICAL_ADDRESS pt=0;
    Check(gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 6, &pt),
          "AllocatePages(PT)");
    Memset((void*)pt, 0, 6*4096);

    auto* pml4 = (UINT64*)pt;
    auto* pdpt = (UINT64*)(pt+4096);
    auto* pd0  = (UINT64*)(pt+4096*2);
    auto* pd1  = (UINT64*)(pt+4096*3);
    auto* pd2  = (UINT64*)(pt+4096*4);
    auto* pd3  = (UINT64*)(pt+4096*5);

    pml4[0]=(UINT64)pdpt|0x03;
    pdpt[0]=(UINT64)pd0|0x03; pdpt[1]=(UINT64)pd1|0x03;
    pdpt[2]=(UINT64)pd2|0x03; pdpt[3]=(UINT64)pd3|0x03;

    for (int i=0;i<512;i++) {
        pd0[i]=(UINT64)(i+0  )*0x200000ULL|0x83;
        pd1[i]=(UINT64)(i+512)*0x200000ULL|0x83;
        pd2[i]=(UINT64)(i+1024)*0x200000ULL|0x83;
        pd3[i]=(UINT64)(i+1536)*0x200000ULL|0x83;
    }
    LOG_VAL("PML4 physical address: ", (UINT64)pt);
    return (UINT64)pt;
}

// ============================================================
// ACPI RSDP
// ============================================================
static UINT64 FindRsdp() {
    TRACE("FindRsdp: scanning EFI config table");
    LOG_DEC("Config table entries: ", gST->NumberOfTableEntries);
    for (UINTN i=0; i<gST->NumberOfTableEntries; ++i) {
        EFI_GUID& g = gST->ConfigurationTable[i].VendorGuid;
        Print("[BL]   Table "); PrintDec(i);
        Print("  GUID=");
        // Print first 4 bytes of GUID for identification
        PrintHex64((UINT64)g.Data1);
        Print("\n");
        EFI_GUID acpi2={0x8868E871,0xE4F1,0x11D3,{0xBC,0x22,0x00,0x80,0xC7,0x3C,0x88,0x81}};
        EFI_GUID acpi1={0xEB9D2D30,0x2D88,0x11D3,{0x9A,0x16,0x00,0x90,0x27,0x3F,0xC1,0x4D}};
        if (GuidEqual(g,acpi2)||GuidEqual(g,acpi1)) {
            UINT64 rsdp=(UINT64)gST->ConfigurationTable[i].VendorTable;
            LOG_VAL("RSDP found at: ", rsdp);
            return rsdp;
        }
    }
    LOG_ERR("RSDP not found");
    return 0;
}

// ============================================================
// Memory map -> BootInfo
// ============================================================
static void FillMemoryMap(MicroNTBootInfo* bi, EFI_MEMORY_DESCRIPTOR* map,
                          UINTN mapSize, UINTN descSize) {
    UINT32 count=0;
    UINT8* p=(UINT8*)map, *end=p+mapSize;
    UINT64 totalMB=0;
    while (p<end && count<BOOT_MEMORY_MAX) {
        auto* d=(EFI_MEMORY_DESCRIPTOR*)p;
        BootMemoryType t;
        switch(d->Type) {
        case EfiConventionalMemory:
        case EfiBootServicesCode:
        case EfiBootServicesData:  t=BOOT_MEM_AVAILABLE; break;
        case EfiLoaderCode:
        case EfiLoaderData:        t=BOOT_MEM_LOADER; break;
        case EfiACPIReclaimMemory: t=BOOT_MEM_ACPI; break;
        case EfiACPIMemoryNVS:     t=BOOT_MEM_NVS; break;
        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace: t=BOOT_MEM_MMIO; break;
        default:                   t=BOOT_MEM_RESERVED; break;
        }
        bi->memory_map[count]={d->PhysicalStart, d->NumberOfPages*4096, t, 0};
        if (t==BOOT_MEM_AVAILABLE) totalMB+=d->NumberOfPages*4/1024;
        ++count; p+=descSize;
    }
    bi->memory_entry_count=count;
    Print("[BL] Memory map: "); PrintDec(count); Print(" entries, ~");
    PrintDec(totalMB); Print(" MB available\n");
}

// ============================================================
// EFI Entry Point
// ============================================================
extern "C" EFI_STATUS EfiMain(EFI_HANDLE ImageHandle,
                               EFI_SYSTEM_TABLE* SystemTable) {
    gST=SystemTable; gBS=SystemTable->BootServices; gConOut=SystemTable->ConOut;

    // Init serial COM1 at 115200 baud (in case UEFI didn't)
    // (just toggle DLAB + set divisor, don't rely on firmware state)
    // Init COM1 (ports > 255 require dx register, use "Nd" constraint)
    __asm__ volatile("outb %0, %1" :: "a"((UINT8)0x00), "Nd"((UINT16)0x3F9)); // disable irqs
    __asm__ volatile("outb %0, %1" :: "a"((UINT8)0x80), "Nd"((UINT16)0x3FB)); // DLAB on
    __asm__ volatile("outb %0, %1" :: "a"((UINT8)0x01), "Nd"((UINT16)0x3F8)); // divisor lo
    __asm__ volatile("outb %0, %1" :: "a"((UINT8)0x00), "Nd"((UINT16)0x3F9)); // divisor hi
    __asm__ volatile("outb %0, %1" :: "a"((UINT8)0x03), "Nd"((UINT16)0x3FB)); // 8N1 DLAB off
    __asm__ volatile("outb %0, %1" :: "a"((UINT8)0xC7), "Nd"((UINT16)0x3FA)); // FIFO on
    __asm__ volatile("outb %0, %1" :: "a"((UINT8)0x0B), "Nd"((UINT16)0x3FC)); // DTR+RTS+OUT2

    if (gConOut) gConOut->ClearScreen(gConOut);
    gBS->SetWatchdogTimer(0,0,0,nullptr);

    Print("\n");
    Print("==================================================\n");
    Print("  MicroNT UEFI Bootloader\n");
    Print("==================================================\n");
    LOG("Starting boot sequence...");

    // ---- Step 1: Loaded image protocol ----
    TRACE("Step 1: Get LoadedImage protocol");
    EFI_LOADED_IMAGE_PROTOCOL* li=nullptr;
    EFI_GUID liGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
    Check(gBS->HandleProtocol(ImageHandle,&liGuid,(void**)&li),
          "HandleProtocol(LoadedImage)");
    LOG_VAL("Image base: ", (UINT64)(UINTN)li->ImageBase);
    LOG_VAL("Image size: ", li->ImageSize);
    LOG_OK("LoadedImage protocol acquired");

    EFI_HANDLE device=li->DeviceHandle;

    // ---- Step 2: Load kernel ELF ----
    TRACE("Step 2: Load kernel ELF from \\boot\\micront.elf");
    void* elfData=nullptr; UINTN elfSize=0;
    LoadFile(device, L"\\boot\\micront.elf", &elfData, &elfSize);
    LOG_OK("Kernel file loaded");

    // ---- Step 3: Parse and load ELF ----
    TRACE("Step 3: Parse ELF and load segments");
    UINT64 kernBase=0, kernEnd=0;
    UINT64 entry=LoadElf(elfData, elfSize, &kernBase, &kernEnd);
    gBS->FreePool(elfData);
    LOG_OK("ELF loaded and segments mapped");

    // ---- Step 4: ACPI RSDP ----
    TRACE("Step 4: Find ACPI RSDP");
    UINT64 rsdp=FindRsdp();

    // ---- Step 5: Page tables ----
    TRACE("Step 5: Build identity-mapped page tables (0-4GB)");
    UINT64 pml4=BuildPageTables();
    __asm__ volatile("mov %0, %%cr3" :: "r"(pml4) : "memory");
    LOG_OK("Page tables installed, CR3 updated");

    // ---- Step 6: Allocate BootInfo ----
    TRACE("Step 6: Allocate MicroNTBootInfo");
    MicroNTBootInfo* bi=nullptr;
    Check(gBS->AllocatePool(EfiLoaderData,sizeof(MicroNTBootInfo),(void**)&bi),
          "AllocatePool(BootInfo)");
    Memset(bi,0,sizeof(MicroNTBootInfo));
    bi->magic=BOOTINFO_MAGIC;
    bi->kernel_phys_base=kernBase;
    bi->kernel_size=kernEnd-kernBase;
    bi->rsdp_phys=rsdp;
    LOG_VAL("BootInfo at: ", (UINT64)(UINTN)bi);

    // ---- Step 7: Memory map + ExitBootServices ----
    TRACE("Step 7: Get memory map and exit boot services");
    UINTN mapSize=0,mapKey=0,descSize=0; UINT32 descVer=0;
    EFI_MEMORY_DESCRIPTOR* memMap=nullptr;

    gBS->GetMemoryMap(&mapSize,nullptr,&mapKey,&descSize,&descVer);
    mapSize += 4*descSize;
    Check(gBS->AllocatePool(EfiLoaderData,mapSize,(void**)&memMap),
          "AllocatePool(MemMap)");
    Check(gBS->GetMemoryMap(&mapSize,memMap,&mapKey,&descSize,&descVer),
          "GetMemoryMap");

    FillMemoryMap(bi,memMap,mapSize,descSize);

    // ExitBootServices retry loop.
    // EFI_INVALID_PARAMETER means the map key went stale (timer IRQ, etc).
    // Refresh the key by calling GetMemoryMap with the EXISTING buffer
    // (never reset mapSize to 0 -- that only queries the required size,
    //  doesn't fill the buffer, and returns a key that is immediately stale).
    {
        EFI_STATUS exSt = EFI_ERR(0x1F);  // arbitrary non-zero to enter loop
        for (int attempt = 0; attempt < 5 && EFI_IS_ERROR(exSt); attempt++) {
            if (attempt > 0) {
                Print("[BL] ExitBootServices key stale, refreshing...\n");
                // Use existing buffer -- just refresh mapKey
                UINTN refreshSize = mapSize;
                gBS->GetMemoryMap(&refreshSize, memMap, &mapKey,
                                  &descSize, &descVer);
                FillMemoryMap(bi, memMap, refreshSize, descSize);
            } else {
                Print("[BL] Calling ExitBootServices...\n");
            }
            exSt = gBS->ExitBootServices(ImageHandle, mapKey);
        }
        if (EFI_IS_ERROR(exSt)) {
            Print("[BL] PANIC: ExitBootServices failed after 5 attempts\n");
            Panic("ExitBootServices", exSt);
        }
    }

    // ---- Boot services gone; serial only from here ----
    SerialStr("[BL] Boot services exited\n");
    SerialStr("[BL] Jumping to kernel...\n");

    // entry = first byte of kernel .text = kernel_phys_base
    // (linker puts .text at KERNEL_PHYS_BASE which is the load base)
    entry = bi->kernel_phys_base;

    SerialStr("[BL] Kernel entry: 0x");
    {
        char hexbuf[17]; hexbuf[16]=0;
        UINT64 ev = entry;
        for (int i=15;i>=0;i--) {
            UINT8 n = ev & 0xF;
            hexbuf[i] = n<10 ? '0'+n : 'A'+n-10;
            ev >>= 4;
        }
        for (int i=0;i<16;i++) SerialPut(hexbuf[i]);
    }
    SerialStr("\n[BL] BootInfo: magic=");
    {
        char hexbuf[17]; hexbuf[16]=0;
        UINT64 ev = bi->magic;
        for (int i=15;i>=0;i--) {
            UINT8 n = ev & 0xF;
            hexbuf[i] = n<10 ? '0'+n : 'A'+n-10;
            ev >>= 4;
        }
        for (int i=0;i<16;i++) SerialPut(hexbuf[i]);
    }
    SerialStr("\n[BL] --- Handing off to kernel ---\n");

    using KernelFn = void(*)(MicroNTBootInfo*);
    auto kfn = reinterpret_cast<KernelFn>(entry);
    kfn(bi);

    SerialStr("[BL] ERROR: kernel returned!\n");
    for(;;) __asm__("cli;hlt");
}
