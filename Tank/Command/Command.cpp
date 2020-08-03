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
#include "Command.h"


void(* resetFunc) (void) = 0; //declare reset function @ address 0


Command::Command(Settings* pCfg, PPMReader* pPppm) {
    _cfg = pCfg;
    _ppm = pPppm;
}
Command::~Command() { }

void Command::setup() {

  Serial.print("\n----------------------------\n");
  Serial.print("  " + PROJECT_NAME + " \n");
  Serial.print("  Author: tropotek.com\n");
  Serial.print("  Version: " + String(_cfg->getVersion()) + " \n");
  Serial.print("  Date: 29-07-2020\n");
  Serial.print("----------------------------\n");
  Serial.print(">> ");
}

void Command::loop() {
    // flush serial buffer ???
    // while(Serial.available()) {
    //     Serial.read(); delay(50);
    // }

    char c;
    while(Serial.available()) {
        c = (char)Serial.read();
        if (c == '\n') {
            cmdFinish = true;
        } else if (c == '\r' || c == '\t' || c == '\0') {
            ; // ingnore
        } else {
            commandStr += c;
            Serial.print(c);
        }
        delay(10);
    }
    if (!commandStr.equals("") && cmdFinish) {
        Serial.println();
        parse(commandStr);
        cmdFinish = false;
        commandStr = "";
        Serial.print("> ");
    }

}

void Command::parse(String cmd) {
    String args[MAX_NUM_ARGS];
    for (int i = 0; i < MAX_NUM_ARGS; i++) args[i] = String("");
    cmd.trim();
    args[0] = cmd;
    if (cmd.indexOf(" ") > -1) {
        for (int i = 0; i < MAX_NUM_ARGS; i++) {
            args[i] = getValue(cmd, ' ', i);
            args[i].toLowerCase();
            args[i].trim();
        }
    }

    // for (int i = 0; i < MAX_NUM_ARGS; i++) {
    //     if (!args[i].equals(""))
    //          Serial.println("===="+String(i)+"> " + args[i]);
    // }

    if (args[0] == "help") {
        cmdHelp();
    } else if (args[0] == "show" || args[0] == "list" || args[0] == "l") {
        cmdShowCfg();
    } else if (args[0] == "save") {
        cmdSaveCfg();
    } else if (args[0] == "reset") {
        cmdInitCfg();
    } else if (args[0] == "read") {
        _cfg->readCfg();
        Serial.println("Settings reloaded from eeprom.");
    } else if (args[0] == "reboot") {
        Serial.println("Reset");
        resetFunc();
    } else if (args[0] == "get") {
        cmdGet(args[1], args[2]);
    } else if (args[0] == "set") {
        cmdSet(args[1], args[2]);
    } 
#if defined(DEBUG)
    // undocumented commands for debugging
    else if (args[0] == "ppmdump" || args[0] == "ppm") {      
        while (true) {
            printPpmChannels();
            delay(100);
        }
    } else if (args[0] == "clear") {
        _cfg->clearCfg();
        _cfg->saveCfg();
        Serial.println("Settings cleared from eeprom and saved.");
    } else if (args[0] == "erase" || args[0] == "format") {
        _cfg->eraseEeprom();
        Serial.println("Eeprom data formatted");
    } 
#endif
    else {
        Serial.println("Command not found!");
    }

}

void Command::cmdGet(String arg, String val) {
    if (arg == "rxmin") {
        Serial.println("rxmin " + String(_cfg->getRxrangeMin()));
    } else if (arg == "rxmax") {
        Serial.println("rxmax " + String(_cfg->getRxrangeMax()));
    } else if (arg == "deadzone") {
        Serial.println("deadzone " + String(_cfg->getDeadzone()));
    } else if (arg == "flutter") {
        Serial.println("flutter " + String(_cfg->getFlutter()));
    } else if (arg == "stickmode") {
        Serial.println("stickMode " + String((int)_cfg->getStickMode()));
    } else {
        Serial.println("Settings parameter not found!");
    }
}
void Command::cmdSet(String arg, String val) {

    if (val == "") {    // TODO: how to deal with missing val arg
        Serial.println("No value parameter specified for: " + arg);
        return;
    }
    if (arg == "rxmin") {
        _cfg->setRxrangeMin(val.toInt());
    } else if (arg == "rxmax") {
        _cfg->setRxrangeMax(val.toInt());
    } else if (arg == "deadzone") {
        _cfg->setDeadzone(val.toInt());
    } else if (arg == "flutter") {
        _cfg->setFlutter(val.toInt());
    } else if (arg == "stickmode") {
        _cfg->setStickMode((bool)val.toInt());
    } else {
        Serial.println("Settings parameter not found!");
    }
}
void Command::cmdHelp(void) {
    Serial.println("Use the following commands:");
    Serial.println("  help:  To view this help.");
    Serial.println("  show:  To show the current settings.");
    Serial.println("  get <param>: Display the value of a settings parameter.");
    Serial.println("  set <param> <value>: Set the value of a settings parameter.");
    Serial.println("  reset:  Factory Reset and save the settings.");
    Serial.println("  read:  Read settings from eeprom.");
    Serial.println("  save:  Save settings to memory.");
#if defined(DEBUG)
    Serial.println("DEBUG Commands:");
    // undocumented commands for debugging
    Serial.println("  ppmdump:  dump the ppm channels received. (requires reset to exit)");
    Serial.println("  clear:  Clear the settings to their zero values.");
    Serial.println("  erase:  <alias: format> Format the eeprom contents. requires reset and save to restore settings.");
#endif
    Serial.println();
}
void Command::cmdShowCfg(void) {
    Serial.println(PROJECT_NAME + " Settings:");
    Serial.println("  version      " + String((int)_cfg->getVersion()));

    Serial.println("  rxMin        " + String((int)_cfg->getRxrangeMin()));
    Serial.println("  rxMax        " + String((int)_cfg->getRxrangeMax()));
    //Serial.println("  deadzone     " + String(_cfg->getDeadzone()));
    Serial.println("  flutter      " + String(_cfg->getFlutter()));
    Serial.println("  stickMode    " + String((int)_cfg->getStickMode()));

    Serial.println("--------------------------------------");
    Serial.println("  Eeprom Size  " + String(EEPROM.length()));
    Serial.println();
}
void Command::cmdSaveCfg(void) {
    _cfg->saveCfg();
    Serial.println("Settings saved.");

}
void Command::cmdInitCfg(void) {
    _cfg->resetCfg();
    _cfg->saveCfg();
    Serial.println("Settings reset and saved.");
}


String Command::getValue(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;
    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void Command::printPpmChannels(void) {
  // Print latest valid values from all channels
  double value;
  String str;
  for (int channel = 1; channel <= MAX_RX_CHANNELS; ++channel) {
      value = _ppm->latestValidChannelValue(channel, 0);
      str += String(value) + " ";
  }
  Serial.println(str);
}
