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
#if BC_CLI
#include <EEPROM.h>
#endif

class Settings {
  public:
    Settings();
    ~Settings();

    void init(void);
    void resetCfg(void);
    bool readCfg(void);
    bool saveCfg(void);
    void eraseEeprom(void);

    void setFlutter(uint8_t i);
    void setDeadzone(uint8_t i);
    void setExpo(int8_t i);
    void setThrottleLimit(uint8_t i);
    void setTxMap(char str[]);
    void setTxMode(uint8_t i);
    void enableReverse(bool b);

    uint8_t getFlutter(void);
    uint8_t getDeadzone(void);
    int8_t getExpo(void);
    uint8_t getThrottleLimit(void);
    char* getTxMap(void);
    uint8_t getTxMode(void);
    bool hasReverse(void);

    String toString(void);

  private:
    const uint16_t TK_EEPROM_ADDR = 100;
    const uint8_t TK_EEPROM_SIG[2] = { 0xee, 0x11 };

    // Struct used here to easly save all setting to the EEPROM
    struct {   
        uint8_t signature[2];                     // TK_EEPROM_SIG
        uint8_t flutter;                          // The amount a channle must change before a value is updated, ignore micro changes (smooths the response)
        uint8_t deadzone;                         // creats a deadzon at the throttle start position
        uint8_t txMode;                           // The controller mode (Default: Mode 2)
        int8_t expo;                              // the amount of expo that can be added (-100 => 100) Negative values slow throttle, positive increase initial thrott
        uint8_t tLimit;                           // Throttle Limit (Default 100%) limit the max and min of the throttle throw by % (0 - 100)
        bool reverse;                             // 0 = forward Only (throttle + rudder used ,non-centerng), 1 = bi directional (Elevator + Aileron used, centering)
        char txMap[10];                           // The controller channel mapping. Default is TAER1234  ???
    } data;
    
};


#endif    /** BC_SETTINGS_H **/
