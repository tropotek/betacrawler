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
 * Define any global objects here an include all libs and headers
 * 
 */

#ifndef TK_HEADERS_H
#define TK_HEADERS_H

/* ***********************************************
 * Global includes
 * ***********************************************/

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif
#include <EEPROM.h>

#include <Servo.h>
#include <ESC.h>
#include "Cli/Cli.h"

#include "configuration.h"
#include "STM32F111CE_pins.h"
#include "PPMReader/PPMReader.h"
#include "Throttle/Throttle.h"
//#include "Settings/Settings.h"
//#include "Mixer/Mixer.h"




#endif   /*  TK_HEADERS_H */
