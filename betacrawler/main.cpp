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

#include "global.h"

/**
 * 
 **/
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);

  Serial.println("\n---------------" + PROJECT_NAME + "-------------");
  Serial.println("  Author:      tropotek.com");
  Serial.println("  Version:     " + String(cfg.getVersion()));
  Serial.println("  Date:        29-07-2020");
  cfg.printSettings();
  Serial.println("---------------------------------------------");
  
  #if (defined(CLI_ENABLED))
    cmd.setup();
  #endif

  mixer.setup();

}

/**
 * 
 **/
void loop() {  
  #if (defined(CLI_ENABLED))
      cmd.loop();
  #endif
  
  mixer.loop();

  // DEBUG: Dump receiver and speed values
  if (false) {
    if (mixer.isArmed()) {
        Serial.println("ls: " + String(mixer.getLeftSpeed()) + "   rs: " + String(mixer.getRightSpeed()));
    } else {
        ppm.printPpmChannels();
    }
  }
  
}


