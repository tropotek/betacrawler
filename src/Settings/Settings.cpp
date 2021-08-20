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


Settings::Settings() { }
Settings::~Settings() { }

void Settings::init() {
    resetCfg();
    if (!readCfg()) {
        saveCfg();
    }
}
void Settings::resetCfg() {
    data.signature[0] = TK_EEPROM_SIG[0];
    data.signature[1] = TK_EEPROM_SIG[1];
    setFlutter(DEFAULT_FLUTTER);
    setDeadzone(DEFAULT_DEADZONE);
    setExpo(DEFAULT_EXPO);
    char map[] = DEFAULT_TX_MAP;
    setTxMap(map);
    setTxMode(DEFAULT_TX_MODE);
    enableReverse(DEFAULT_REVERSE);
}

bool Settings::readCfg(void) {
#ifdef EEPROM_h
    EEPROM.get(TK_EEPROM_ADDR, data);
    if (data.signature[0] != TK_EEPROM_SIG[0] && data.signature[1] != TK_EEPROM_SIG[1]) {
        // TODO: We need to ouotput the error somehow as this could be called before the serial is initalized
        //Serial.println("Error 1001: Reset and save your settings.");
        return false;
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

void Settings::setDeadzone(uint8_t i) {
    i = constrain(i, 0, 100);
    data.deadzone = (uint8_t)i;
}

void Settings::setExpo(int8_t i) {
    i = constrain(i, -100, 100);
    data.expo = (int8_t)i;
}

void Settings::setTxMode(uint8_t i) {
    i = constrain(i, 1, 4);
    data.txMode = (uint8_t)i;
}

void Settings::enableReverse(bool b) {
    data.reverse = b;
}

void Settings::setTxMap(char str[]) {
    memset(data.txMap,0,sizeof(data.txMap));
    for(int i = 0; i < strlen(str); i++) {
        data.txMap[i] = str[i];
    }
}

uint8_t Settings::getFlutter(void) {
    return data.flutter;
}

uint8_t Settings::getDeadzone(void) {
    return data.deadzone;
}

int8_t Settings::getExpo(void) {
    return data.expo;
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
    str += " deadzone       " + String(getDeadzone()) + "\n";
    str += " expo           " + String(getExpo()) + "\n";
    str += " txmode         " + String(getTxMode()) + "\n";
    String rev = hasReverse() ? "Enabled" : "Disabled";
    str += " reverse        " + rev + "\n";
    str += " txmap          " + String(getTxMap()) + "\n";
#if (defined(EEPROM_h) && defined(DEBUG))
    str += " -------------- DEBUG -----------------\n";
    str += " eeprom         [" + String(EEPROM.length()) + "]\n";
    str += " --------------------------------------\n";
#endif
    return str;
}