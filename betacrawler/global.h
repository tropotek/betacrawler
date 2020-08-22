/*
 * Copyright (C) 2020  www.tropotek.com
 * Project: Betacrawler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TK_GLOBAL_H
#define TK_GLOBAL_H



/* ***********************************************
 * Global includes
 * ***********************************************/

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

#include <EEPROM.h>

#include "configuration.h"

#include "Settings/Settings.h"

#include "PPMReader/PPMReader.h"
#include "Mixer/Mixer.h"

#if (defined(CLI_ENABLED))
  #include "Command/Command.h"
#endif

/* ***********************************************
 * Global variables
 * ***********************************************/

Settings cfg;
PPMReader ppm(PPM_RX_PIN, MAX_RX_CHANNELS);
Mixer mixer(&cfg, &ppm);

#if (defined(CLI_ENABLED))
  Command cmd(&cfg, &ppm);
#endif


#endif   /*  TK_GLOBAL_H */
