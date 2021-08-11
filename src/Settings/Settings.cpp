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
    setFlutter(FLUTTER);
    char map[] = TX_MAP;
    setTxMap(map);
    setTxMode(RX_MODE);
    enableReverse(REVERSE);
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


int Settings::getFlutter(void) {
    return (int)_cfg.flutter;
}

char* Settings::getTxMap(void) {
    return _cfg.txMap;
}

int Settings::getTxMode(void) {
    return (int)_cfg.txMode;
}

bool Settings::hasReverse(void) {
    return (bool)_cfg.reverse;
}


void Settings::setFlutter(int i) {
    i = constrain(i, 0, 50);
    _cfg.flutter = i;
}

void Settings::setTxMap(char* str) {
    for(int i = 0; i < sizeof(str); i++) {
        _cfg.txMap[i] = str[i];
    }
}

void Settings::setTxMode(int i) {
    i = constrain(i, 500, 2500);
    _cfg.txMode = i;
}

void Settings::enableReverse(bool b) {
    _cfg.reverse = b;
}

void Settings::printSettings(void) {
    Serial.println("Settings:");
    Serial.println("  flutter      " + String(getFlutter()));
    //Serial.println("  txMap        " + String(getTxMap()));         // TODO: ???
    Serial.println("  txMode       " + String(getTxMode()));
    String sm = hasReverse() ? "Enabled" : "Disabled";
    Serial.println("  reverse      " + sm);
#if (defined(BC_CLI) && defined(DEBUG))
        //Serial.println("  eeprom       " + String(EEPROM.length()));
#endif
}