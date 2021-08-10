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
 *
 * Sources:
 *   https://robotic-controls.com/learn/arduino/organized-eeprom-storage
 *
 *
 */

#ifndef BC_SETTINGS_H
#define BC_SETTINGS_H

#include "configuration.h"

//#if (defined(CLI_ENABLED))
#include <EEPROM.h>
//#endif

typedef struct
{   
    uint8_t signature[2];                 // TK_EEPROM_SIG
    uint8_t version;                      // Build version: if 255, or < VERSION update config vals struct  ????
    uint8_t deadzone;                     // Center stick Deadzone, Only really needed when reverse is enabled
    uint8_t stickMode;                    // TODO: I think we need to get rid of this in favor of some sort of channel mapping system
} configData_t;

// TODO: How to move these consts to the class without it barfing???
const uint16_t TK_EEPROM_ADDR = 100;
const uint8_t TK_EEPROM_SIG[2] = { 0xee, 0x11 };

class Settings {
  public:
     Settings();
    ~Settings();

    void resetCfg(void);
    bool readCfg(void);
    bool saveCfg(void);
    void clearCfg(void);
    void eraseEeprom(void);

    int getVersion(void);
    int getRxrangeMin(void);
    int getRxrangeMax(void);
    int getDeadzone(void);
    int getFlutter(void);
    bool getStickMode(void);

    void setRxrangeMin(int i);
    void setRxrangeMax(int i);
    void setDeadzone(int i);
    void setFlutter(int i);
    void setStickMode(bool b);

    void printSettings(void);

  private:
    configData_t _cfg;
    
};


#endif    /** BC_SETTINGS_H **/
