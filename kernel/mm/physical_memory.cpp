// physical_memory.cpp - MicroNT bitmap PMM (UEFI memory map path)

#include "../include/memory.h"
#include "../include/bootinfo.h"
#include "../include/debug.h"

namespace {

// Bitmap: 1 bit per 4 KB page, supports up to 8 GB
constexpr usize MAX_PAGES    = (8ULL * 1024 * 1024 * 1024) / PAGE_SIZE;
constexpr usize BITMAP_BYTES = MAX_PAGES / 8;

static u8    s_bitmap[BITMAP_BYTES];
static usize s_total_pages = 0;
static usize s_free_pages  = 0;
static usize s_search_hint = 0;

static void BitmapSet(usize page) {
    s_bitmap[page / 8] |= (u8)(1u << (page & 7));
}
static void BitmapClear(usize page) {
    s_bitmap[page / 8] &= (u8)~(1u << (page & 7));
}
static bool BitmapTest(usize page) {
    return (s_bitmap[page / 8] >> (page & 7)) & 1;
}

} // namespace

namespace PMM {

void Init(MicroNTBootInfo* bi) {
    // Start with everything reserved
    for (usize i = 0; i < BITMAP_BYTES; ++i) s_bitmap[i] = 0xFF;

    if (!bi || bi->magic != BOOTINFO_MAGIC) {
        KDBG_ERROR("PMM: invalid boot info (magic=0x%llx)",
            bi ? (u64)bi->magic : 0ULL);
        return;
    }

    // Walk UEFI memory map entries from the bootloader
    for (u32 i = 0; i < bi->memory_entry_count; ++i) {
        const auto& e = bi->memory_map[i];

        bool is_free = (e.type == BOOT_MEM_AVAILABLE);

        u64 base = AlignUp<u64>((u64)e.base, (u64)PAGE_SIZE);
        u64 end  = AlignDown<u64>((u64)(e.base + e.length), (u64)PAGE_SIZE);
        if (base >= end) continue;

        for (u64 addr = base; addr < end; addr += PAGE_SIZE) {
            usize page = (usize)(addr / PAGE_SIZE);
            if (page >= MAX_PAGES) break;

            if (is_free) {
                BitmapClear(page);
                ++s_free_pages;
                ++s_total_pages;
            } else {
                // Not free, but count as "known" pages for total
                ++s_total_pages;
            }
        }
    }

    // Reserve the bitmap itself
    u64 bm_start = reinterpret_cast<u64>(s_bitmap);
    u64 bm_end   = bm_start + BITMAP_BYTES;
    for (u64 a = AlignDown(bm_start, (u64)PAGE_SIZE);
         a < AlignUp(bm_end, (u64)PAGE_SIZE); a += PAGE_SIZE) {
        usize pg = (usize)(a / PAGE_SIZE);
        if (pg < MAX_PAGES && !BitmapTest(pg)) {
            BitmapSet(pg);
            if (s_free_pages) --s_free_pages;
        }
    }

    // Reserve first 2 MB (low memory, real-mode IVT, BIOS data, etc.)
    for (usize page = 0; page < (2 * 1024 * 1024 / PAGE_SIZE); ++page) {
        if (!BitmapTest(page)) {
            BitmapSet(page);
            if (s_free_pages) --s_free_pages;
        }
    }
}

u64 AllocPage() {
    for (usize i = s_search_hint; i < MAX_PAGES; ++i) {
        if (!BitmapTest(i)) {
            BitmapSet(i);
            if (s_free_pages) --s_free_pages;
            s_search_hint = i + 1;
            return (u64)i * PAGE_SIZE;
        }
    }
    KDBG_ERROR("PMM: out of physical pages!");
    return 0;
}

void FreePage(u64 phys_addr) {
    usize page = (usize)(phys_addr / PAGE_SIZE);
    if (page >= MAX_PAGES || !BitmapTest(page)) {
        KDBG_WARN("PMM: FreePage of invalid addr 0x%llx", phys_addr);
        return;
    }
    BitmapClear(page);
    ++s_free_pages;
    if (page < s_search_hint) s_search_hint = page;
}

u64 AllocPages(usize count) {
    if (count == 0) return 0;
    usize run = 0;
    for (usize i = 0; i < MAX_PAGES; ++i) {
        if (!BitmapTest(i)) {
            if (++run == count) {
                usize start = i - count + 1;
                for (usize j = start; j <= i; ++j) {
                    BitmapSet(j);
                    if (s_free_pages) --s_free_pages;
                }
                return (u64)start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }
    KDBG_ERROR("PMM: cannot allocate %llu contiguous pages", (u64)count);
    return 0;
}

usize TotalPages() { return s_total_pages; }
usize FreePages()  { return s_free_pages;  }
usize UsedPages()  { return s_total_pages - s_free_pages; }

} // namespace PMM
