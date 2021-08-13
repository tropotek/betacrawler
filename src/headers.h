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
 * Include all libs and headers
 * 
 */

#ifndef BC_HEADERS_H
#define BC_HEADERS_H

/* ***********************************************
 * Global includes
 * ***********************************************/

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

#include <Servo.h>
#include <ESC.h>

#include "configuration.h"
#include "pins/pins.h"

#include "Settings/Settings.h"
#include "Cmd/Cmd.h"
//#include <EEPROM.h>
#include "PPMReader/PPMReader.h"
#include "Throttle/Throttle.h"
#include "Mixer/Mixer.h"
#include "Betacrawler.h"



#endif   /*  BC_HEADERS_H */
