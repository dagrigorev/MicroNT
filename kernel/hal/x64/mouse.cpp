// mouse.cpp -- MicroNT PS/2 auxiliary mouse driver.

#include "../../include/ntdef.h"
#include "../../include/hal.h"
#include "../../include/debug.h"

namespace MOUSE {

static constexpr u16 PS2_DATA = 0x60;
static constexpr u16 PS2_STATUS = 0x64;
static constexpr u16 PS2_COMMAND = 0x64;

static Packet s_ring[32];
static volatile u32 s_head = 0;
static volatile u32 s_tail = 0;
static u8 s_packet[3] = {};
static u32 s_packet_index = 0;
static i32 s_x = 1700;
static i32 s_y = 560;
static bool s_ready = false;
static u32 s_log_budget = 24;

static u8 Inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static void Outb(u16 port, u8 value) {
    __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port));
}

static bool WaitInputClear() {
    for (u32 i = 0; i < 100000; ++i) {
        if ((Inb(PS2_STATUS) & 0x02) == 0) return true;
    }
    return false;
}

static bool WaitOutputFull() {
    for (u32 i = 0; i < 100000; ++i) {
        if (Inb(PS2_STATUS) & 0x01) return true;
    }
    return false;
}

static bool WriteController(u8 command) {
    if (!WaitInputClear()) return false;
    Outb(PS2_COMMAND, command);
    return true;
}

static bool WriteData(u8 value) {
    if (!WaitInputClear()) return false;
    Outb(PS2_DATA, value);
    return true;
}

static bool ReadData(u8& value) {
    if (!WaitOutputFull()) return false;
    value = Inb(PS2_DATA);
    return true;
}

static bool WriteMouse(u8 command) {
    if (!WriteController(0xD4)) return false;
    if (!WriteData(command)) return false;

    u8 ack = 0;
    if (!ReadData(ack)) return false;
    return ack == 0xFA;
}

static void Push(const Packet& packet) {
    u32 next = (s_tail + 1) & 31;
    if (next == s_head) return;
    s_ring[s_tail] = packet;
    s_tail = next;
}

static void ApplyPacket() {
    u8 b0 = s_packet[0];
    i32 dx = static_cast<i8>(s_packet[1]);
    i32 dy = static_cast<i8>(s_packet[2]);

    s_x += dx;
    s_y -= dy;
    if (s_x < 0) s_x = 0;
    if (s_y < 0) s_y = 0;
    if (s_x > 1896) s_x = 1896;
    if (s_y > 1048) s_y = 1048;

    Packet packet{};
    packet.DeltaX = dx;
    packet.DeltaY = dy;
    packet.Left = (b0 & 0x01) != 0;
    packet.Right = (b0 & 0x02) != 0;
    packet.Middle = (b0 & 0x04) != 0;
    Push(packet);

    VGA::MoveMouseCursor(static_cast<u32>(s_x), static_cast<u32>(s_y));

    if (s_log_budget > 0) {
        --s_log_budget;
        Debug::Printf("[MOUSE] move dx=%d dy=%d pos=(%d,%d) buttons=%c%c%c\r\n",
                      dx, dy, s_x, s_y,
                      packet.Left ? 'L' : '-',
                      packet.Right ? 'R' : '-',
                      packet.Middle ? 'M' : '-');
    }
}

void Init() {
    s_head = 0;
    s_tail = 0;
    s_packet_index = 0;
    s_x = 1700;
    s_y = 560;
    s_ready = false;
    s_log_budget = 24;

    while (Inb(PS2_STATUS) & 0x01) (void)Inb(PS2_DATA);

    if (!WriteController(0xA8)) {
        Debug::Print("[MOUSE] PS/2 auxiliary port enable failed\r\n");
        return;
    }

    if (!WriteController(0x20)) {
        Debug::Print("[MOUSE] PS/2 controller config read failed\r\n");
        return;
    }

    u8 config = 0;
    if (!ReadData(config)) {
        Debug::Print("[MOUSE] PS/2 controller config unavailable\r\n");
        return;
    }

    config |= 0x02;   // enable IRQ12
    config &= ~0x20;  // enable auxiliary clock
    if (!WriteController(0x60) || !WriteData(config)) {
        Debug::Print("[MOUSE] PS/2 controller config write failed\r\n");
        return;
    }

    if (!WriteMouse(0xF6) || !WriteMouse(0xF4)) {
        Debug::Print("[MOUSE] PS/2 mouse did not acknowledge setup\r\n");
        return;
    }

    s_ready = true;
    Debug::Print("[MOUSE] PS/2 mouse initialized (IRQ12, packets enabled)\r\n");
}

bool IsReady() {
    return s_ready;
}

bool TryRead(Packet* out) {
    if (!out || s_head == s_tail) return false;
    *out = s_ring[s_head];
    s_head = (s_head + 1) & 31;
    return true;
}

bool CurrentPosition(i32* x, i32* y) {
    if (!s_ready) return false;
    if (x) *x = s_x;
    if (y) *y = s_y;
    return true;
}

void HandleIrq(u8 /*irq*/) {
    u8 status = Inb(PS2_STATUS);
    if ((status & 0x01) == 0) return;

    u8 data = Inb(PS2_DATA);
    if (s_packet_index == 0 && (data & 0x08) == 0) return;

    s_packet[s_packet_index++] = data;
    if (s_packet_index == 3) {
        s_packet_index = 0;
        ApplyPacket();
    }
}

} // namespace MOUSE
