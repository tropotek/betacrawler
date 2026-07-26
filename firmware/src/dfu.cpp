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
  // initVariant() below finds it.
  NVIC_SystemReset();
}

}  // namespace dfu

// Called by the Arduino core's main() before setup() -- and therefore before
// Serial.begin() initialises USB. Declared weak in the core's Arduino.h, so
// simply defining it here overrides it.
//
// Jumping from a *fresh* boot with the USB peripheral untouched is the whole
// point. Jumping out of a running application with an active USB stack is the
// approach that famously costs days: the host still holds an open CDC device,
// the peripheral is mid-transaction, and the bootloader inherits the mess.
void initVariant() {
  unlockBackupDomain();
  if (RTC->BKP0R != kDfuMagic) return;

  // Cleared BEFORE the jump, not after. If anything below fails, the next
  // reset must come up as a normal application -- a board that reboots
  // forever into a bootloader that never appeared is indistinguishable from
  // a brick, and would need SWD to recover.
  RTC->BKP0R = 0;

  __disable_irq();

  // Undo everything premain()'s init() set up. The ROM bootloader expects
  // reset-state peripherals and the HSI clock; leaving the PLL running or a
  // peripheral clocked makes its USB enumeration unreliable.
  HAL_RCC_DeInit();
  HAL_DeInit();

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  // Map system memory at 0x00000000 so the bootloader's vector table is where
  // the core will look for it.
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

  // Re-enabled before the jump: the bootloader drives USB from interrupts and
  // will hang with them masked.
  __enable_irq();

  const uint32_t* sysmem = reinterpret_cast<const uint32_t*>(DFU_SYSMEM_ADDR);
  void (*bootJump)(void) = reinterpret_cast<void (*)(void)>(sysmem[1]);
  __set_MSP(sysmem[0]);
  bootJump();

  // Not reached. If it somehow is, fall through into setup() as a normal boot
  // rather than spinning -- the magic is already cleared, so the app runs.
}

#else   // !FEATURE_DFU

namespace dfu {

// A board with no ROM bootloader (or one that deliberately does not expose
// it) answers `nodfu` and advertises no `dfu` capability. No initVariant() is
// defined, so the core's weak default applies and the binary is unchanged.
bool DfuTrigger::supported() const { return false; }
bool DfuTrigger::enterDfu()        { return false; }
void DfuTrigger::reboot()          {}

}  // namespace dfu

#endif  // FEATURE_DFU
