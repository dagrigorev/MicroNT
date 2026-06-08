#pragma once
// vmsvga.h -- VMware SVGA II display driver (VirtualBox --graphicscontroller
// vmsvga). Gives the kernel runtime control of the display resolution and the
// linear framebuffer, instead of relying on the pre-boot UEFI GOP mode.

#include "ntdef.h"

namespace VMSVGA {

// Probe the PCI bus for the VMware SVGA II device (15AD:0405) and initialize
// the register/FIFO interface. Returns true if the device is present.
bool Init();

bool IsPresent();

// Program a display mode (32 bpp) and repoint the VGA framebuffer at the
// device's linear framebuffer. Returns true on success.
bool SetMode(u32 width, u32 height);

// Flush the framebuffer to the host display (SVGA_CMD_UPDATE over the FIFO).
// No-op when the device is absent.
void Present();

} // namespace VMSVGA
