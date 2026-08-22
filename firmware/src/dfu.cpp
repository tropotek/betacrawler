#include "dfu.h"

#if FEATURE_DFU

#include <Arduino.h>

#ifndef DFU_SYSMEM_ADDR
#error "FEATURE_DFU is on but the board header defines no DFU_SYSMEM_ADDR"
#endif

namespace {

// Arbitrary, but deliberately not 0 and not 0xFFFFFFFF: those are the values a
// cleared or uninitialised backup register is most likely to hold, and either
// would make an unrelated reset look like a DFU request.
constexpr uint32_t kDfuMagic = 0xB00710ADu;

// RTC backup register 0. Chosen over a .noinit RAM variable because it does
// not depend on the linker script keeping a section out of .bss, and over a
// GPIO/BOOT0 trick because it needs no wiring.
//
// Unlocking is required before every write: the backup domain is
// write-protected out of reset (PWR_CR's DBP bit), and a silent no-op write
// is exactly the failure that turns into "the reboot button does nothing".
void unlockBackupDomain() {
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
}

}  // namespace

namespace dfu {

bool DfuTrigger::supported() const { return true; }

bool DfuTrigger::enterDfu() {
  // Arms only. The reset must not happen until main.cpp has flushed the
  // response -- see the comment on core::Bootloader.
  pending_ = true;
  return true;
}

void DfuTrigger::reboot() {
  unlockBackupDomain();
  RTC->BKP0R = kDfuMagic;
  // A system reset leaves the backup domain (and so BKP0R) intact, which is
  // the entire mechanism: the magic survives into the next boot, where
  // dfuJumpOnBoot() below finds it.
  NVIC_SystemReset();
}

}  // namespace dfu

// Runs from .preinit_array, which __libc_init_array() walks before any
// constructor -- and so before the Arduino core's premain() (itself a
// constructor) calls init() and brings the USB peripheral up. That ordering is
// the whole point: entering from initVariant() meant jumping on top of a live,
// already-enumerated CDC device, and tearing that down first left the host with
// an attach/detach/attach burst it sometimes refused to enumerate after -- the
// board reached the ROM bootloader's poll loop but never reappeared on the bus.
// Here there is nothing to tear down: the clocks are still SystemInit's
// defaults and USB does not exist yet, which is as close to a hardware BOOT0
// reset as software gets.
static void dfuJumpOnBoot();

__attribute__((used, section(".preinit_array")))
static void (*const kDfuPreinit)(void) = dfuJumpOnBoot;

static void dfuJumpOnBoot() {
  unlockBackupDomain();
  if (RTC->BKP0R != kDfuMagic) return;

  // Cleared BEFORE the jump, not after. If anything below fails, the next reset
  // must come up as a normal application -- a board that reboots forever into a
  // bootloader that never appeared is indistinguishable from a brick, and would
  // need SWD to recover.
  RTC->BKP0R = 0;

  __disable_irq();

  // Map system memory at 0x00000000 so the bootloader's vector table is where
  // the core will look for it.
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

  // Back to the reset value. SystemInit() pointed it at the application's
  // vector table, and leaving it there sends the bootloader's own interrupts --
  // USB's among them -- into this firmware's handlers.
  SCB->VTOR = 0;

  const uint32_t* sysmem = reinterpret_cast<const uint32_t*>(DFU_SYSMEM_ADDR);
  void (*bootJump)(void) = reinterpret_cast<void (*)(void)>(sysmem[1]);
  __set_MSP(sysmem[0]);

  // The bootloader drives USB from interrupts and hangs with them masked.
  __enable_irq();
  bootJump();

  // Not reached. If it somehow is, returning boots the application normally --
  // the magic is already cleared.
}

#else   // !FEATURE_DFU

namespace dfu {

// A board with no ROM bootloader (or one that deliberately does not expose
// it) answers `nodfu` and advertises no `dfu` capability. No boot-time
// constructor is defined either, so the binary is unchanged.
bool DfuTrigger::supported() const { return false; }
bool DfuTrigger::enterDfu()        { return false; }
void DfuTrigger::reboot()          {}

}  // namespace dfu

#endif  // FEATURE_DFU
