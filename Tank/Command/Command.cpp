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
    //commandStr.reserve(200);
    commandStr = "";
}
Command::~Command() { }

void Command::setup() {

}

void Command::loop() {
    char c;
    while(Serial.available()) {
        c = Serial.read();
        if (c == '\n') {
            cmdFinish = true;
        } else if (c == '\r' || c == '\t' || c == '\0') {
            ; // ingnore
        } else {
            commandStr += c;
            Serial.print(c);
        }
    }
    if (!commandStr.equals("") && cmdFinish) {
        Serial.println();
        parse(commandStr);
        cmdFinish = false;
        commandStr = "";
        Serial.print("> ");
    }
}

void Command::parse(String cmdStr) {
    for (int i = 0; i < MAX_NUM_ARGS; i++) {
        args[i] = "";  // reset cmd array
        //args[i].reserve(16);
    }
    cmdStr.trim();
    args[0] = cmdStr;
    if (cmdStr.indexOf(" ") > -1) {
        for (int i = 0; i < MAX_NUM_ARGS; i++) {
            args[i] = getValue(cmdStr, ' ', i);
            args[i].toLowerCase();
            args[i].trim();
            if (args[i].equals("")) break;
        }
    }

    // for (int i = 0; i < MAX_NUM_ARGS; i++) {
    //     if (!args[i].equals(""))
    //          Serial.println("===="+String(i)+"> " + args[i]);
    // }

    if (args[0].equals("help") || args[0].equals("h") || args[0].equals("?")) {
        cmdHelp();
    } else if (args[0].equals("show") || args[0].equals("list") || args[0].equals("l")) {
        cmdShowCfg();
    } else if (args[0].equals("save")) {
        cmdSaveCfg();
    } else if (args[0].equals("reset")) {
        cmdInitCfg();
    } else if (args[0].equals("read")) {
        _cfg->readCfg();
        Serial.println("Settings reloaded from eeprom.");
    } else if (args[0].equals("reboot")) {
        Serial.println("Rebooting....");
        resetFunc();
    } else if (args[0].equals("get")) {
        cmdGet(args[1], args[2]);
    } else if (args[0].equals("set")) {
        cmdSet(args[1], args[2]);
    } 
#if defined(DEBUG)
    // undocumented commands for debugging
    else if (args[0].equals("ppmdump") || args[0].equals("ppm")) {
        while (true) {
            _ppm->printPpmChannels();
        }
    } else if (args[0].equals("clear")) {
        _cfg->clearCfg();
        _cfg->saveCfg();
        Serial.println("Settings cleared from eeprom and saved.");
    } else if (args[0].equals("erase") || args[0].equals("format")) {
        _cfg->eraseEeprom();
        Serial.println("Eeprom data formatted");
    } 
#endif
    else {
        Serial.println("Command not found!");
    }

}

void Command::cmdGet(String arg, String val) {
    if (arg.equals("rxmin")) {
        Serial.println("rxmin " + String(_cfg->getRxrangeMin()));
    } else if (arg.equals("rxmax")) {
        Serial.println("rxmax " + String(_cfg->getRxrangeMax()));
    } else if (arg.equals("deadzone")) {
        Serial.println("deadzone " + String(_cfg->getDeadzone()));
    } else if (arg.equals("flutter")) {
        Serial.println("flutter " + String(_cfg->getFlutter()));
    } else if (arg.equals("stickmode") || arg.equals("sm")) {
        Serial.println("stickMode " + String((int)_cfg->getStickMode()));
    } else {
        Serial.println("Settings parameter not found!");
    }
}
void Command::cmdSet(String arg, String val) {

    if (val.equals("")) {    // TODO: how to deal with missing val arg
        Serial.println("No value parameter specified for: " + arg);
        return;
    }
    if (arg.equals("rxmin")) {
        _cfg->setRxrangeMin(val.toInt());
    } else if (arg.equals("rxmax")) {
        _cfg->setRxrangeMax(val.toInt());
    } else if (arg.equals("deadzone")) {
        _cfg->setDeadzone(val.toInt());
    } else if (arg.equals("flutter")) {
        _cfg->setFlutter(val.toInt());
    } else if (arg.equals("stickmode") || arg.equals("sm")) {
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
    String sm = ((int)_cfg->getStickMode() == 0) ? "0 [SINGLE STICK]" : "1 [DUAL STICK]";
    Serial.println("  stickMode    " + sm);
#if defined(DEBUG)
    Serial.println("--------------------------------------");
    //Serial.println("  Eeprom Size  " + String(EEPROM.length()));
#endif
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
