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
#include "Settings.h"


Settings::Settings() {
    resetCfg();
    readCfg();
}
Settings::~Settings() { }

void Settings::resetCfg() {
    data.signature[0] = TK_EEPROM_SIG[0];
    data.signature[1] = TK_EEPROM_SIG[1];
    setFlutter(FLUTTER);
    char map[] = TX_MAP;
    setTxMap(map);
    setTxMode(TX_MODE);
    enableReverse(REVERSE);
}

bool Settings::readCfg(void) {
#ifdef EEPROM_h
    EEPROM.get(TK_EEPROM_ADDR, _cfg);
     if (data.signature[0] != TK_EEPROM_SIG[0] && 
         data.signature[1] != TK_EEPROM_SIG[1]) {
       Serial.println("Error 1001: Reset and save your settings.");
       return(false);
    }
#endif
    return true;
}

bool Settings::saveCfg(void) {
#ifdef EEPROM_h
    EEPROM.put(TK_EEPROM_ADDR, data);
#endif
    return true;
}

void Settings::eraseEeprom(void) {
#ifdef EEPROM_h
    for (uint8_t i = 0 ; i < EEPROM.length() ; i++) {
        EEPROM.write(i, 0);
    }
#endif
}


void Settings::setFlutter(uint8_t i) {
    i = constrain(i, 0, 100);
    data.flutter = (uint8_t)i;
}

void Settings::setTxMode(uint8_t i) {
    i = constrain(i, 1, 4);
    data.txMode = (uint8_t)i;
}

void Settings::enableReverse(bool b) {
    data.reverse = b;
}

void Settings::setTxMap(char str[]) {
    for(int i = 0; i < strlen(str); i++) {
        data.txMap[i] = str[i];
    }
}


uint8_t Settings::getFlutter(void) {
    return data.flutter;
}

uint8_t Settings::getTxMode(void) {
    return data.txMode;
}

bool Settings::hasReverse(void) {
    return data.reverse;
}

char* Settings::getTxMap(void) {
    return data.txMap;
}


String Settings::toString(void) {
    String str;
    str = "Settings:\n";
    str += " version        " + String(VERSION) + "\n";
    str += " flutter        " + String(getFlutter()) + "\n";
    str += " txMode         " + String(getTxMode()) + "\n";
    String rev = hasReverse() ? "Enabled" : "Disabled";
    str += " reverse        " + rev + "\n";
    str += " txMap          " + String(getTxMap()) + "\n";
#if (defined(EEPROM_h) && defined(DEBUG))
    str += " eeprom         " + String(EEPROM.length()) + "\n";
#endif
    return str;
}