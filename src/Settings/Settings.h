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
 * Manage the Betacrawler settings storage values here.
 * If the BC_CLI option is enabled these can be changed by Serial interface
 * 
 *
 * Sources:
 *   https://robotic-controls.com/learn/arduino/organized-eeprom-storage
 * 
 *
 */

#ifndef BC_SETTINGS_H
#define BC_SETTINGS_H

#include "configuration.h"


typedef struct
{   
    uint8_t signature[2];                     // TK_EEPROM_SIG
    uint8_t flutter;                          // The amount a channle must change before a value is updated, ignore micro changes (smooths the response)
    uint8_t txMode;                           // The controller mode (Default: Mode 2)
    bool reverse;                             // 0 = forward Only (throttle + rudder used ,non-centerng), 1 = bi directional (Elevator + Aileron used, centering)
    char txMap[5];                            // The controller channel mapping. Default is TAER  ???
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
    void eraseEeprom(void);

    void setFlutter(uint8_t i);
    void setTxMap(char str[]);
    void setTxMode(uint8_t i);
    void enableReverse(bool b);

    uint8_t getFlutter(void);
    char* getTxMap(void);
    uint8_t getTxMode(void);
    bool hasReverse(void);

    String toString(void);

  private:
    configData_t _cfg;
    
};


#endif    /** BC_SETTINGS_H **/
