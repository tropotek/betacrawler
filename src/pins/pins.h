/*
 * Copyright (C) 2021  www.tropotek.com
 * Project: Betacrawler
 * Facebook: https://www.facebook.com/groups/307432330496662/
 * Github: https://github.com/tropotek/betacrawler 
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * ***********************************************************************
 * 
 */

#ifndef BC_PINS_H
#define BC_PINS_H
// Architecture specific include
#if defined(BC_AVR)
#include "avr.h"
#elif defined(BC_STM32F4)
#include "stm32f411ce.h"
// #elif defined(ARDUINO_ARCH_SAM)
// #include "sam/ServoTimers.h"
// #elif defined(ARDUINO_ARCH_SAMD)
// #include "samd/ServoTimers.h"
// #elif defined(ARDUINO_ARCH_NRF52)
// #include "nrf52/ServoTimers.h"
// #elif defined(ARDUINO_ARCH_MEGAAVR)
// #include "megaavr/ServoTimers.h"
// #elif defined(ARDUINO_ARCH_MBED)
// #include "mbed/ServoTimers.h"
#else
//#error "This library only supports boards with an AVR, SAM, SAMD, NRF52 or STM32F4 processor."
#error "Betacrawler only supports boards with an AVR, or STM32F4 processor at this time."
#endif


#endif   /*  BC_PINS_H */
