#include "dfu.h"

#if FEATURE_DFU

#include <Arduino.h>

#ifndef DFU_SYSMEM_ADDR
#error "FEATURE_DFU is on but the board header defines no DFU_SYSMEM_ADDR"
#endif

#if defined(USBCON) && defined(USBD_USE_CDC)
// The Arduino core's own USB device handle (libraries/USBDevice/src/usbd_conf.c),
// global but declared in no header. initVariant() below needs it to properly
// tear the USB peripheral down before jumping -- see the comment there for why.
extern PCD_HandleTypeDef g_hpcd;
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
// Serial.begin() runs. Declared weak in the core's Arduino.h, so simply
// defining it here overrides it.
//
// NOT called before USB itself comes up, though -- that was this function's
// original assumption, and it was wrong. The core's premain() (a
// constructor-priority hook that runs before main(), see
// cores/arduino/main.cpp) already calls init() -> hw_config_init() ->
// USBD_CDC_init() by the time initVariant() gets a chance to run. So the USB
// peripheral is already live, enumerated, and mid-flight as a CDC device
// here -- not "fresh and untouched" as the comment used to claim. Jumping
// into the ROM bootloader on top of that half-alive peripheral left it stuck
// in a state where neither the CDC descriptor nor a DFU one would ever
// enumerate again: confirmed on real hardware by halting the core over SWD
// after the jump -- the CPU was genuinely running inside the ROM bootloader's
// own poll loop (PC sitting in the 0x1FFFxxxx system memory region, moving
// normally between halts), yet the board never reappeared on the USB bus in
// EITHER mode, even left running undisturbed for several seconds. The
// bootloader was alive and waiting; USB just never came back.
//
// The fix is to properly tear the USB peripheral down first, the same way
// the framework's own USBD_reenumerate() forces a fresh re-enumeration at
// normal startup: HAL_PCD_DeInit() calls HAL_PCD_Stop() internally, which
// sets the peripheral's soft-disconnect bit and releases the D+ pull-up so
// the host actually sees a disconnect, then disables the USB clock via
// HAL_PCD_MspDeInit(). Only after that does the ROM bootloader get a clean
// peripheral to bring its own USB stack up on.
#if defined(USBCON) && defined(USBD_USE_CDC)
static void teardownUsb() {
  HAL_PCD_DeInit(&g_hpcd);
}
#else
static void teardownUsb() {}
#endif

void initVariant() {
  unlockBackupDomain();
  if (RTC->BKP0R != kDfuMagic) return;

  // Cleared BEFORE the jump, not after. If anything below fails, the next
  // reset must come up as a normal application -- a board that reboots
  // forever into a bootloader that never appeared is indistinguishable from
  // a brick, and would need SWD to recover.
  RTC->BKP0R = 0;

  __disable_irq();

  // Must happen before HAL_RCC_DeInit()/HAL_DeInit() below: HAL_PCD_DeInit()
  // still needs a live peripheral clock to touch the USB registers safely.
  teardownUsb();

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
