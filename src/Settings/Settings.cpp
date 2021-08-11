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
#include "Settings.h"

Settings::Settings() {
    resetCfg();
    readCfg();
}
Settings::~Settings() { }

void Settings::resetCfg() {
    _cfg.signature[0] = TK_EEPROM_SIG[0];
    _cfg.signature[1] = TK_EEPROM_SIG[1];

    setFlutter(15);
    char map[] = "TEAR";
    setTxMap(map);
    setTxMode(2);
    enableReverse(false);

    // setFlutter(FLUTTER);
    // char map[] = TX_MAP;
    // setTxMap(map);
    // setTxMode(TX_MODE);
    // enableReverse(REVERSE);
}

bool Settings::readCfg(void) {
#ifdef EEPROM_h
    EEPROM.get(TK_EEPROM_ADDR, _cfg);
     if (_cfg.signature[0] != TK_EEPROM_SIG[0] && 
         _cfg.signature[1] != TK_EEPROM_SIG[1]) {
       Serial.println("001: Reset your configuration.");
       return(false);
    }
#endif
    return true;
}

bool Settings::saveCfg(void) {
#ifdef EEPROM_h
    EEPROM.put(TK_EEPROM_ADDR, _cfg);
#endif
    return(true);
}

void Settings::eraseEeprom(void) {
#ifdef EEPROM_h
    for (uint8_t i = 0 ; i < EEPROM.length() ; i++) {
        EEPROM.write(i, 0);
    }
#endif
}


void Settings::setFlutter(uint8_t i) {
    //i = constrain(i, 0, 100);
    _cfg.flutter = (uint8_t)i;
}

void Settings::setTxMode(uint8_t i) {
    //i = constrain(i, 1, 4);
    _cfg.txMode = (uint8_t)i;
}

void Settings::enableReverse(bool b) {
    _cfg.reverse = b;
}

void Settings::setTxMap(char str[]) {
    for(int i = 0; i < strlen(str); i++) {
        _cfg.txMap[i] = str[i];
    }
}


uint8_t Settings::getFlutter(void) {
    return _cfg.flutter;
}

uint8_t Settings::getTxMode(void) {
    return _cfg.txMode;
}

bool Settings::hasReverse(void) {
    return _cfg.reverse;
}

char* Settings::getTxMap(void) {
    return _cfg.txMap;
}


String Settings::toString(void) {
    String str;
    //Serial.printf("%.*s\n", (int)sizeof(settings.getTxMap()), settings.getTxMap());

    str = "Settings:\n";
    str += " version        " + String(VERSION) + "\n";

    str += " flutter        " + String(getFlutter()) + "\n";
    str += " txMode         " + String(getTxMode()) + "\n";
    String rev = hasReverse() ? "Enabled" : "Disabled";
    str += " reverse        " + rev + "\n";
    
    str += " txMap          " + String(_cfg.txMap) + "\n";
    str += " txMap          " + String(getTxMap()) + "\n";
    
#if (defined(EEPROM_h) && defined(DEBUG))
    str += " eeprom         " + String(EEPROM.length()) + "\n";
#endif
    return str;
}