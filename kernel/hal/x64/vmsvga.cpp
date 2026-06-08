// vmsvga.cpp -- VMware SVGA II display driver.
//
// Implements just enough of the VMware SVGA II interface to own the display:
// PCI discovery, the legacy index/value register port protocol, a minimal
// command FIFO, runtime mode setting, and full-screen UPDATE flushing. This
// is what lets MicroNT pick its own resolution at runtime rather than being
// stuck with whatever mode the UEFI GOP handed over at boot.

#include "../../include/ntdef.h"
#include "../../include/hal.h"
#include "../../include/debug.h"

namespace VMSVGA {

// ---- PCI configuration space (mechanism #1) -------------------------------
static constexpr u16 PCI_CONFIG_ADDR = 0xCF8;
static constexpr u16 PCI_CONFIG_DATA = 0xCFC;

static u32 PciRead32(u8 bus, u8 slot, u8 func, u8 off) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)slot << 11) |
               ((u32)func << 8) | (off & 0xFCu);
    HAL::IoOutDword(PCI_CONFIG_ADDR, addr);
    return HAL::IoInDword(PCI_CONFIG_DATA);
}

static void PciWrite32(u8 bus, u8 slot, u8 func, u8 off, u32 value) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)slot << 11) |
               ((u32)func << 8) | (off & 0xFCu);
    HAL::IoOutDword(PCI_CONFIG_ADDR, addr);
    HAL::IoOutDword(PCI_CONFIG_DATA, value);
}

// ---- VMware SVGA II constants ---------------------------------------------
static constexpr u16 VMW_VENDOR = 0x15AD;
static constexpr u16 VMW_SVGA2  = 0x0405;

static constexpr u32 SVGA_INDEX_PORT = 0;
static constexpr u32 SVGA_VALUE_PORT = 1;

enum SvgaReg : u32 {
    SVGA_REG_ID            = 0,
    SVGA_REG_ENABLE        = 1,
    SVGA_REG_WIDTH         = 2,
    SVGA_REG_HEIGHT        = 3,
    SVGA_REG_MAX_WIDTH     = 4,
    SVGA_REG_MAX_HEIGHT    = 5,
    SVGA_REG_BITS_PER_PIXEL = 7,
    SVGA_REG_BYTES_PER_LINE = 12,
    SVGA_REG_FB_START      = 13,
    SVGA_REG_FB_OFFSET     = 14,
    SVGA_REG_VRAM_SIZE     = 15,
    SVGA_REG_FB_SIZE       = 16,
    SVGA_REG_CAPABILITIES  = 17,
    SVGA_REG_FIFO_START    = 18,
    SVGA_REG_FIFO_SIZE     = 19,
    SVGA_REG_CONFIG_DONE   = 20,
    SVGA_REG_SYNC          = 21,
    SVGA_REG_BUSY          = 22,
};

static constexpr u32 SVGA_ID_2 = 0x90000002u;
static constexpr u32 SVGA_ID_1 = 0x90000001u;
static constexpr u32 SVGA_ID_0 = 0x90000000u;

// FIFO register slots (u32 indices) and commands.
enum SvgaFifo : u32 {
    SVGA_FIFO_MIN      = 0,
    SVGA_FIFO_MAX      = 1,
    SVGA_FIFO_NEXT_CMD = 2,
    SVGA_FIFO_STOP     = 3,
};
static constexpr u32 SVGA_CMD_UPDATE = 1;

// ---- Driver state ----------------------------------------------------------
static bool s_present = false;
static u16  s_io_base = 0;
static volatile u32* s_fifo = nullptr;
static u32  s_fifo_size = 0;
static u32  s_width = 0;
static u32  s_height = 0;

static u32 RegRead(u32 index) {
    HAL::IoOutDword(s_io_base + SVGA_INDEX_PORT, index);
    return HAL::IoInDword(s_io_base + SVGA_VALUE_PORT);
}

static void RegWrite(u32 index, u32 value) {
    HAL::IoOutDword(s_io_base + SVGA_INDEX_PORT, index);
    HAL::IoOutDword(s_io_base + SVGA_VALUE_PORT, value);
}

static bool FindDevice(u8& bus_out, u8& slot_out, u8& func_out) {
    for (u16 bus = 0; bus < 256; ++bus) {
        for (u8 slot = 0; slot < 32; ++slot) {
            for (u8 func = 0; func < 8; ++func) {
                u32 id = PciRead32((u8)bus, slot, func, 0x00);
                u16 vendor = (u16)(id & 0xFFFF);
                u16 device = (u16)(id >> 16);
                if (vendor == 0xFFFF) {
                    if (func == 0) break;   // no device in this slot
                    continue;
                }
                if (vendor == VMW_VENDOR && device == VMW_SVGA2) {
                    bus_out = (u8)bus; slot_out = slot; func_out = func;
                    return true;
                }
            }
        }
    }
    return false;
}

bool Init() {
    s_present = false;

    u8 bus = 0, slot = 0, func = 0;
    if (!FindDevice(bus, slot, func)) {
        Debug::Print("[VMSVGA] VMware SVGA II not found on PCI\r\n");
        return false;
    }

    // BAR0 = I/O ports, BAR1 = framebuffer (VRAM), BAR2 = FIFO MMIO.
    u32 bar0 = PciRead32(bus, slot, func, 0x10);
    u32 bar2 = PciRead32(bus, slot, func, 0x18);
    if ((bar0 & 0x1) == 0) {
        Debug::Print("[VMSVGA] BAR0 is not an I/O BAR - unsupported\r\n");
        return false;
    }
    s_io_base = (u16)(bar0 & 0xFFFCu);
    u64 fifo_phys = (u64)(bar2 & 0xFFFFFFF0u);

    // Enable I/O space, memory space and bus mastering.
    u32 cmd = PciRead32(bus, slot, func, 0x04);
    PciWrite32(bus, slot, func, 0x04, cmd | 0x7u);

    // Negotiate the highest SVGA ID the device accepts.
    RegWrite(SVGA_REG_ID, SVGA_ID_2);
    u32 id = RegRead(SVGA_REG_ID);
    if (id != SVGA_ID_2) {
        RegWrite(SVGA_REG_ID, SVGA_ID_1);
        id = RegRead(SVGA_REG_ID);
        if (id != SVGA_ID_1) {
            RegWrite(SVGA_REG_ID, SVGA_ID_0);
            id = RegRead(SVGA_REG_ID);
            if (id != SVGA_ID_0) {
                Debug::Print("[VMSVGA] SVGA ID negotiation failed\r\n");
                return false;
            }
        }
    }

    u32 max_w = RegRead(SVGA_REG_MAX_WIDTH);
    u32 max_h = RegRead(SVGA_REG_MAX_HEIGHT);
    u32 vram  = RegRead(SVGA_REG_VRAM_SIZE);
    s_fifo_size = RegRead(SVGA_REG_FIFO_SIZE);

    s_fifo = reinterpret_cast<volatile u32*>(fifo_phys);

    Debug::Printf("[VMSVGA] SVGA II at %u:%u.%u io=0x%x fifo=0x%llx "
                  "max=%ux%u vram=%uMB\r\n",
                  (u32)bus, (u32)slot, (u32)func, (u32)s_io_base, fifo_phys,
                  max_w, max_h, vram / (1024 * 1024));

    s_present = true;
    return true;
}

bool IsPresent() { return s_present; }

static void FifoInit() {
    if (!s_fifo) return;
    s_fifo[SVGA_FIFO_MIN]      = 16;            // 4 registers * 4 bytes
    s_fifo[SVGA_FIFO_MAX]      = s_fifo_size;
    s_fifo[SVGA_FIFO_NEXT_CMD] = 16;
    s_fifo[SVGA_FIFO_STOP]     = 16;
    RegWrite(SVGA_REG_CONFIG_DONE, 1);
}

static void FifoPush(u32 value) {
    if (!s_fifo) return;
    u32 next = s_fifo[SVGA_FIFO_NEXT_CMD];
    s_fifo[next / 4] = value;
    next += 4;
    if (next >= s_fifo[SVGA_FIFO_MAX]) next = s_fifo[SVGA_FIFO_MIN];
    s_fifo[SVGA_FIFO_NEXT_CMD] = next;
}

bool SetMode(u32 width, u32 height) {
    if (!s_present) return false;

    RegWrite(SVGA_REG_ENABLE, 0);
    RegWrite(SVGA_REG_WIDTH, width);
    RegWrite(SVGA_REG_HEIGHT, height);
    RegWrite(SVGA_REG_BITS_PER_PIXEL, 32);
    RegWrite(SVGA_REG_ENABLE, 1);

    u32 fb_phys   = RegRead(SVGA_REG_FB_START);
    u32 fb_offset = RegRead(SVGA_REG_FB_OFFSET);
    u32 pitch     = RegRead(SVGA_REG_BYTES_PER_LINE);
    if (fb_phys == 0 || pitch == 0) {
        Debug::Print("[VMSVGA] SetMode: bad framebuffer/pitch\r\n");
        return false;
    }

    FifoInit();

    s_width = width;
    s_height = height;
    u32 stride_px = pitch / 4;   // 32 bpp

    // VMware SVGA framebuffers are 32-bit BGRX (format index 1 in our VGA).
    VGA::SetFramebuffer((u64)fb_phys + fb_offset, width, height, stride_px, 1);

    Debug::Printf("[VMSVGA] mode set %ux%u fb=0x%x pitch=%u stride=%u\r\n",
                  width, height, fb_phys + fb_offset, pitch, stride_px);
    return true;
}

void Present() {
    if (!s_present || !s_fifo || s_width == 0) return;
    FifoPush(SVGA_CMD_UPDATE);
    FifoPush(0);
    FifoPush(0);
    FifoPush(s_width);
    FifoPush(s_height);
    RegWrite(SVGA_REG_SYNC, 1);
    // Drain: wait for the device to consume the command.
    u32 guard = 0;
    while (RegRead(SVGA_REG_BUSY) != 0 && ++guard < 100000) { }
}

} // namespace VMSVGA
