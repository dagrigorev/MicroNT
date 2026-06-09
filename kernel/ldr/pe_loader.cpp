// pe_loader.cpp - MicroNT M8 PE32+ loader with import resolution

#include "../include/pe.h"
#include "../include/memory.h"
#include "../include/debug.h"
#include "../include/ntstatus.h"
#include "../include/process.h"

// ============================================================
// Internal PE constants
// ============================================================
namespace {

[[maybe_unused]] constexpr u32 IMAGE_NT_SIGNATURE   = 0x00004550u;
constexpr u16 IMAGE_AMD64          = 0x8664;
constexpr u16 IMAGE_PE32PLUS       = 0x020Bu;

[[maybe_unused]] constexpr u32 DIR_EXPORT  = 0;
[[maybe_unused]] constexpr u32 DIR_IMPORT  = 1;

// Section flags -> PTE flags
u64 SectionPteFlags(u32 chars) {
    u64 f = VMM::PTE_PRESENT | VMM::PTE_USER;
    if (chars & 0x80000000u) f |= VMM::PTE_WRITABLE;   // MEM_WRITE
    return f;
}

// Freestanding strcmp
bool streq(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
usize strlen_s(const char* s) {
    usize n=0; while(s[n]) ++n; return n;
}

// ============================================================
// Module registry
// ============================================================
struct Module {
    char     name[32];
    u64      image_base;
    const u8* pe_data;
    usize    pe_size;
};
constexpr usize MAX_MODULES = 64;   // headroom: ntdll is re-registered per process
static Module   s_mods[MAX_MODULES];
static u32      s_mod_count = 0;

static void RegisterModule(const char* name, u64 base,
                            const u8* data, usize size) {
    // Append-only (matches the original loader). The same DLL is re-registered
    // once per process; FindModule returns the first match. MAX_MODULES has
    // enough headroom for every ntdll load plus kernel32.
    if (s_mod_count >= MAX_MODULES) {
        KDBG_ERROR("LDR: module registry full, dropping '%s'", name);
        return;
    }
    auto& m = s_mods[s_mod_count++];
    usize n = strlen_s(name);
    if (n >= 32) n = 31;
    for (usize i=0;i<n;++i) m.name[i]=name[i];
    m.name[n]='\0';
    m.image_base = base;
    m.pe_data    = data;
    m.pe_size    = size;
    KDBG_TRACE("LDR: registered module '%s' base=0x%llx", name, base);
}

static Module* FindModule(const char* name) {
    for (u32 i=0;i<s_mod_count;++i)
        if (streq(s_mods[i].name, name)) return &s_mods[i];
    return nullptr;
}

// ============================================================
// Export table lookup (by name, inside a static PE blob)
// Returns absolute VA of the named export, or 0 if not found.
// ============================================================
static u64 FindExport(const Module& mod, const char* func_name) {
    const u8* base = mod.pe_data;
    u32 e_lfanew = *reinterpret_cast<const u32*>(base+60);
    const u8* pe  = base + e_lfanew;
    // Optional header starts at pe+4+20
    const u8* opt = pe + 4 + 20;
    // DataDirectory[0] = export directory
    u32 exp_rva = *reinterpret_cast<const u32*>(opt+112);
    if (!exp_rva) return 0;

    // Translate export RVA to file offset (section walk)
    // Simple approach: RVA -> file offset using section headers
    u16 num_sects  = *reinterpret_cast<const u16*>(pe+4+2);
    u16 opt_sz     = *reinterpret_cast<const u16*>(pe+4+16);
    const u8* shdrs = opt + opt_sz;

    auto rva_to_off = [&](u32 rva) -> u32 {
        for (u16 s=0;s<num_sects;++s) {
            const u8* sh = shdrs + s*40;
            u32 v_rva  = *reinterpret_cast<const u32*>(sh+12);
            u32 v_size = *reinterpret_cast<const u32*>(sh+ 8);
            u32 raw_off = *reinterpret_cast<const u32*>(sh+20);
            if (rva >= v_rva && rva < v_rva + v_size)
                return raw_off + (rva - v_rva);
        }
        return 0;
    };

    u32 exp_off = rva_to_off(exp_rva);
    if (!exp_off) return 0;
    const u8* ed = base + exp_off;

    u32 num_names = *reinterpret_cast<const u32*>(ed+24);
    u32 fn_rva    = *reinterpret_cast<const u32*>(ed+28);  // AddressOfFunctions
    u32 nm_rva    = *reinterpret_cast<const u32*>(ed+32);  // AddressOfNames
    u32 or_rva    = *reinterpret_cast<const u32*>(ed+36);  // AddressOfNameOrdinals

    u32 fn_off = rva_to_off(fn_rva);
    u32 nm_off = rva_to_off(nm_rva);
    u32 or_off = rva_to_off(or_rva);
    if (!fn_off || !nm_off || !or_off) return 0;

    const u32* names  = reinterpret_cast<const u32*>(base+nm_off);
    const u16* ords   = reinterpret_cast<const u16*>(base+or_off);
    const u32* funcs  = reinterpret_cast<const u32*>(base+fn_off);

    for (u32 i=0; i<num_names; ++i) {
        u32 name_off = rva_to_off(names[i]);
        if (!name_off) continue;
        if (streq(reinterpret_cast<const char*>(base+name_off), func_name)) {
            u32 func_rva = funcs[ords[i]];
            u64 va = mod.image_base + func_rva;
            KDBG_TRACE("LDR: resolved %s -> 0x%llx", func_name, va);
            return va;
        }
    }
    return 0;
}

// ============================================================
// Patch one IAT slot with the resolved VA.
// iat_va: virtual address (in the loading process's PML4) of the 8-byte slot.
// ============================================================
static bool PatchIATSlot(u64 pml4_phys, u64 iat_va, u64 func_va) {
    u64 phys = VMM::TranslateInPml4(pml4_phys, iat_va);
    if (!phys) {
        KDBG_ERROR("LDR: PatchIAT: VA 0x%llx not mapped", iat_va);
        return false;
    }
    u64 page_off = iat_va & 0xFFF;
    *reinterpret_cast<u64*>(phys - page_off + page_off) = func_va;
    // simpler: phys already IS phys = page_base + page_off
    *reinterpret_cast<u64*>(phys) = func_va;
    return true;
}

// ============================================================
// Parse & map a PE into a user PML4, resolve imports.
// ============================================================
static NTSTATUS MapSections(const u8* base, usize pe_size,
                             u64 pml4_phys, u64 load_base) {
    u32 e_lfanew = *reinterpret_cast<const u32*>(base+60);
    const u8* pe  = base + e_lfanew + 4;
    u16 num_sects = *reinterpret_cast<const u16*>(pe+2);
    u16 opt_sz    = *reinterpret_cast<const u16*>(pe+16);
    const u8* opt  = pe + 20;
    const u8* shdrs = opt + opt_sz;

    // Map the PE headers at the image base (read-only, user). Windows maps the
    // headers here; GetProcAddress / RtlImageNtHeader read e_lfanew and the data
    // directories from base+0. Sections begin at RVA >= 0x1000 so no overlap.
    {
        u64 hphys = PMM::AllocPage();
        if (!hphys) return STATUS_INSUFFICIENT_RESOURCES;
        u8* hp = reinterpret_cast<u8*>(hphys);
        usize hcopy = pe_size < PAGE_SIZE ? pe_size : PAGE_SIZE;
        for (usize i=0;i<PAGE_SIZE;++i) hp[i] = (i < hcopy) ? base[i] : 0;
        if (!VMM::MapPageInto(pml4_phys, load_base, hphys,
                              VMM::PTE_PRESENT | VMM::PTE_USER)) {
            PMM::FreePage(hphys);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    for (u16 s=0; s<num_sects; ++s) {
        const u8* sh = shdrs + s*40;
        u32 v_size  = *reinterpret_cast<const u32*>(sh+ 8);
        u32 v_rva   = *reinterpret_cast<const u32*>(sh+12);
        u32 raw_size= *reinterpret_cast<const u32*>(sh+16);
        u32 raw_off = *reinterpret_cast<const u32*>(sh+20);
        u32 chars   = *reinterpret_cast<const u32*>(sh+36);
        if (!v_size || !v_rva) continue;

        char nm[9]={}; for(int i=0;i<8;++i) nm[i]=sh[i];
        u64 pte_flags = SectionPteFlags(chars);
        u64 sect_va   = load_base + v_rva;
        usize pages   = (v_size + 0xFFF) / 0x1000;

        KDBG_INFO("LDR: map '%s' rva=0x%x va=0x%llx pages=%llu",
                  nm, v_rva, sect_va, (u64)pages);

        for (usize pg=0; pg<pages; ++pg) {
            u64 phys = PMM::AllocPage();
            if (!phys) return STATUS_INSUFFICIENT_RESOURCES;
            u8* p = reinterpret_cast<u8*>(phys);
            for (usize i=0;i<PAGE_SIZE;++i) p[i]=0;

            usize file_off = raw_off + pg*PAGE_SIZE;
            if (raw_off && raw_size && file_off < pe_size) {
                usize copy = PAGE_SIZE;
                if (file_off + copy > pe_size) copy = pe_size - file_off;
                usize sect_end = raw_off + raw_size;
                if (file_off >= sect_end) copy = 0;
                if (copy) for (usize i=0;i<copy;++i) p[i]=base[file_off+i];
            }
            if (!VMM::MapPageInto(pml4_phys, sect_va+pg*PAGE_SIZE, phys, pte_flags)) {
                PMM::FreePage(phys);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS ResolveImports(const u8* base, usize /*pe_size*/,
                                u64 pml4_phys, u64 load_base) {
    u32 e_lfanew = *reinterpret_cast<const u32*>(base+60);
    const u8* pe  = base + e_lfanew + 4;
    u16 num_sects = *reinterpret_cast<const u16*>(pe+2);
    u16 opt_sz    = *reinterpret_cast<const u16*>(pe+16);
    const u8* opt  = pe + 20;
    u32 imp_rva   = *reinterpret_cast<const u32*>(opt+112+1*8);
    if (!imp_rva) return STATUS_SUCCESS;  // no imports

    // rva_to_off helper (same as FindExport)
    const u8* shdrs = opt + opt_sz;
    auto rva_to_off = [&](u32 rva) -> u32 {
        for (u16 s=0;s<num_sects;++s) {
            const u8* sh = shdrs + s*40;
            u32 vr = *reinterpret_cast<const u32*>(sh+12);
            u32 vs = *reinterpret_cast<const u32*>(sh+ 8);
            u32 ro = *reinterpret_cast<const u32*>(sh+20);
            if (rva >= vr && rva < vr+vs) return ro+(rva-vr);
        }
        return 0;
    };

    u32 imp_off = rva_to_off(imp_rva);
    if (!imp_off) return STATUS_INVALID_IMAGE_FORMAT;

    // Walk IMAGE_IMPORT_DESCRIPTORs (20 bytes each)
    for (u32 idx=0; ; ++idx) {
        const u8* id = base + imp_off + idx*20;
        u32 int_rva = *reinterpret_cast<const u32*>(id+ 0);
        u32 dll_rva = *reinterpret_cast<const u32*>(id+12);
        u32 iat_rva = *reinterpret_cast<const u32*>(id+16);
        if (!dll_rva && !iat_rva) break;  // null terminator

        u32 dll_off = rva_to_off(dll_rva);
        if (!dll_off) continue;
        const char* dll_name = reinterpret_cast<const char*>(base+dll_off);
        KDBG_INFO("LDR: import DLL '%s'", dll_name);

        Module* mod = FindModule(dll_name);
        if (!mod) {
            KDBG_ERROR("LDR: module '%s' not found in registry", dll_name);
            return STATUS_NOT_FOUND;
        }

        // Walk INT (IMAGE_THUNK_DATA64) and patch IAT
        u32 int_off = rva_to_off(int_rva ? int_rva : iat_rva);
        if (!int_off) continue;

        for (u32 slot=0; ; ++slot) {
            u64 thunk = *reinterpret_cast<const u64*>(base+int_off+slot*8);
            if (!thunk) break;

            const char* fname;
            if (thunk >> 63) {
                // Import by ordinal (not supported in M8)
                KDBG_WARN("LDR: ordinal import not supported"); continue;
            } else {
                // Import by name: thunk = RVA of IMAGE_IMPORT_BY_NAME
                u32 ibn_off = rva_to_off((u32)thunk);
                if (!ibn_off) continue;
                fname = reinterpret_cast<const char*>(base+ibn_off+2);
            }

            u64 func_va = FindExport(*mod, fname);
            if (!func_va) {
                KDBG_ERROR("LDR: export '%s' not found in '%s'", fname, dll_name);
                return STATUS_NOT_FOUND;
            }

            // IAT slot VA in the target process
            u64 iat_slot_va = load_base + iat_rva + slot*8;
            if (!PatchIATSlot(pml4_phys, iat_slot_va, func_va)) {
                KDBG_ERROR("LDR: PatchIAT failed at slot %u", slot);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            KDBG_INFO("LDR: IAT[%u] '%s' -> 0x%llx", slot, fname, func_va);
        }
    }
    return STATUS_SUCCESS;
}

} // anonymous namespace

// ============================================================
// Public API
// ============================================================
namespace LDR {

void Init() {
    KDBG_INFO("LDR: PE loader initialized (M8 - import resolution)");
}

NTSTATUS LoadPe(const void* pe_data, usize pe_size,
                u64 pml4_phys, u64 load_base, u64* entry_out) {
    if (!pe_data || pe_size < 64 || !entry_out) return STATUS_INVALID_PARAMETER;
    const u8* base = static_cast<const u8*>(pe_data);

    if (base[0]!='M'||base[1]!='Z') return STATUS_INVALID_IMAGE_FORMAT;
    u32 e = *reinterpret_cast<const u32*>(base+60);
    if (base[e]!='P'||base[e+1]!='E') return STATUS_INVALID_IMAGE_FORMAT;
    if (*reinterpret_cast<const u16*>(base+e+4) != IMAGE_AMD64)
        return STATUS_INVALID_IMAGE_FORMAT;

    const u8* opt = base + e + 4 + 20;
    if (*reinterpret_cast<const u16*>(opt) != IMAGE_PE32PLUS)
        return STATUS_INVALID_IMAGE_FORMAT;

    u32 entry_rva      = *reinterpret_cast<const u32*>(opt+16);
    u64 preferred_base = *reinterpret_cast<const u64*>(opt+24);

    KDBG_INFO("LDR: loading PE at 0x%llx (preferred 0x%llx) entry_rva=0x%x",
              load_base, preferred_base, entry_rva);

    // 1. Map sections
    NTSTATUS st = MapSections(base, pe_size, pml4_phys, load_base);
    if (!NT_SUCCESS(st)) return st;

    // 2. Resolve imports
    st = ResolveImports(base, pe_size, pml4_phys, load_base);
    if (!NT_SUCCESS(st)) return st;

    // 3. Record the image base in the process PEB / PEB->Ldr (Windows compat).
    PS::NotifyImageLoaded(pml4_phys, load_base);

    *entry_out = load_base + entry_rva;
    KDBG_INFO("LDR: entry -> 0x%llx", *entry_out);
    return STATUS_SUCCESS;
}

NTSTATUS LoadAndRegister(const char* name,
                          const void* pe_data, usize pe_size,
                          u64 pml4_phys, u64 load_base, u64* entry_out) {
    NTSTATUS st = LoadPe(pe_data, pe_size, pml4_phys, load_base, entry_out);
    if (NT_SUCCESS(st))
        RegisterModule(name, load_base, static_cast<const u8*>(pe_data), pe_size);
    return st;
}

u64 GetModuleBase(const char* name) {
    Module* m = FindModule(name);
    return m ? m->image_base : 0;
}

// On-demand DLL catalog (name -> blob + preferred base).
struct CatalogEntry { char name[32]; const u8* data; usize size; u64 base; };
static CatalogEntry s_catalog[16];
static u32          s_catalog_count = 0;

void AddCatalog(const char* name, const void* pe_data, usize pe_size, u64 base) {
    if (s_catalog_count >= 16) return;
    auto& c = s_catalog[s_catalog_count++];
    usize n = strlen_s(name); if (n >= 32) n = 31;
    for (usize i=0;i<n;++i) c.name[i]=name[i];
    c.name[n]='\0';
    c.data = static_cast<const u8*>(pe_data);
    c.size = pe_size;
    c.base = base;
    KDBG_TRACE("LDR: catalog '%s' base=0x%llx", name, base);
}

u64 LoadLibrary(const char* name, u64 pml4_phys) {
    // Already registered (e.g. mapped at process creation)? Return its base.
    if (Module* m = FindModule(name)) return m->image_base;
    // Otherwise map it from the catalog into this address space.
    for (u32 i=0;i<s_catalog_count;++i) {
        if (!streq(s_catalog[i].name, name)) continue;
        u64 entry = 0;
        NTSTATUS st = LoadAndRegister(name, s_catalog[i].data, s_catalog[i].size,
                                      pml4_phys, s_catalog[i].base, &entry);
        if (!NT_SUCCESS(st)) return 0;
        KDBG_INFO("LDR: LoadLibrary('%s') -> 0x%llx", name, s_catalog[i].base);
        return s_catalog[i].base;
    }
    KDBG_ERROR("LDR: LoadLibrary('%s') not in catalog", name);
    return 0;
}

} // namespace LDR
