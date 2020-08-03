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
    // TODO: Implement a reliable CLI with interupts
    // if (!readCfg()) {
    //    resetCfg();
    //    saveCfg();
    // }
}
Settings::~Settings() { }

void Settings::resetCfg() {
    _cfg.signature[0] = TK_EEPROM_SIG[0];
    _cfg.signature[1] = TK_EEPROM_SIG[1];
    _cfg.version = VERSION;
    setRxrangeMin(RX_MIN);
    setRxrangeMax(RX_MAX);
    setDeadzone(DEADZONE);
    setFlutter(FLUTTER);
    setStickMode(STICK_MODE);
}
bool Settings::readCfg(void) {
    // EEPROM.get(TK_EEPROM_ADDR, _cfg);
    //  if (_cfg.signature[0] != TK_EEPROM_SIG[0] && 
    //      _cfg.signature[1] != TK_EEPROM_SIG[1]) {
    //    Serial.println("001: Reset your configuration.");
    //    return(false);
    // }
    // // handle any version adjustments here
    // if (_cfg.version != VERSION) {
    //   // do something here
    //   Serial.println("Upgrading your settings for v"+String(VERSION));
    //   if (_cfg.version == 0 && VERSION == 1) {
    //       resetCfg();
    //   }
    //   // TODO: Add updates when developing new versions
    //   //if (_cfg.version == 1 && VERSION == 2) {
    //   //}

    //   _cfg.version = VERSION;
    //   saveCfg();
    // }
    return(true);
}
bool Settings::saveCfg(void) {
    //EEPROM.put(TK_EEPROM_ADDR, _cfg);
    return(true);
}
void Settings::clearCfg(void) {
    _cfg.signature[0] = 0;
    _cfg.signature[1] = 0;
    _cfg.version = 0;
    setRxrangeMin(0);
    setRxrangeMax(0);
    setDeadzone(0);
    setFlutter(0);
    setStickMode(false);
}

void Settings::eraseEeprom(void) {
    // for (uint8_t i = 0 ; i < EEPROM.length() ; i++) {
    //     EEPROM.write(i, 0);
    // }
}


int Settings::getVersion(void) {
    return (int)_cfg.version;
}
int Settings::getRxrangeMin(void) {
    return (int)_cfg.rxrangeMin;
}
int Settings::getRxrangeMax(void) {
    return (int)_cfg.rxrangeMax;
}
int Settings::getDeadzone(void) {
    return (int)_cfg.deadzone;
}
int Settings::getFlutter(void) {
    return (int)_cfg.flutter;
}
bool Settings::getStickMode(void) {
    return (bool)_cfg.stickMode;
}


void Settings::setRxrangeMin(int i) {
    i = constrain(i, 500, 2500);
    _cfg.rxrangeMin = (uint16_t)i;
}
void Settings::setRxrangeMax(int i) {
    i = constrain(i, 500, 2500);
    _cfg.rxrangeMax = (uint16_t)i;
}
void Settings::setDeadzone(int i) {
    i = constrain(i, 0, 200);
    _cfg.deadzone = (uint8_t)i;
}
void Settings::setFlutter(int i) {
    i = constrain(i, 0, 50);
    _cfg.flutter = (uint8_t)i;
}
void Settings::setStickMode(bool b) {
    _cfg.stickMode = (uint8_t)b;
}

void Settings::printSettings(void) {
    //Serial.println("Settings:");
    Serial.println("  rxMin        " + String((int)getRxrangeMin()));
    Serial.println("  rxMax        " + String((int)getRxrangeMax()));
    //Serial.println("  deadzone     " + String(getDeadzone()));
    Serial.println("  flutter      " + String(getFlutter()));
    String sm = getStickMode() ? "1 [SINGLE STICK]" : "0 [DUAL STICK]";
    Serial.println("  stickMode    " + sm);
}