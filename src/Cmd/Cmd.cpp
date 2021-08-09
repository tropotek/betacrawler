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
 * Sources:
 *   https://www.norwegiancreations.com/2018/02/creating-a-command-line-interface-in-arduinos-serial-monitor/
 * 
 */
#include "Cmd.h"


void(* resetFunc) (void) = 0; //declare reset function @ address 0


Cmd::Cmd(Stream *streamObject) {
    serial = streamObject;
    commandStr = "";
}
Cmd::~Cmd() { }

void Cmd::setup() {
    serial->print("> ");
}

void Cmd::loop() {
    char c;
    while(serial->available()) {
        c = serial->read();
        if (c == '\n') {
            cmdFinish = true;
        } else if (c == '\r' || c == '\t' || c == '\0') {
            ; // ingnore
        } else {
            commandStr += c;
            serial->print(c);
        }
    }
    if (!commandStr.equals("") && cmdFinish) {
        serial->println();
        parse(commandStr);
        cmdFinish = false;
        commandStr = "";
        serial->print("> ");
    }
}

void Cmd::parse(String cmdStr) {
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
    //          serial->println("===="+String(i)+"> " + args[i]);
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
        //_cfg->readCfg();
        serial->println("Settings reloaded from eeprom.");
    } else if (args[0].equals("reboot")) {
        // serial->println("Rebooting....");
        // resetFunc();
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
        serial->println("Settings cleared from eeprom and saved.");
    } else if (args[0].equals("erase") || args[0].equals("format")) {
        _cfg->eraseEeprom();
        serial->println("Eeprom data formatted");
    } 
#endif
    else {
        serial->println("Command not found!");
    }

}

void Cmd::cmdGet(String arg, String val) {
    serial->println("---- Not implemented ----");
    
    // if (arg.equals("rxmin")) {
    //     serial->println("rxmin " + String(_cfg->getRxrangeMin()));
    // } else if (arg.equals("rxmax")) {
    //     serial->println("rxmax " + String(_cfg->getRxrangeMax()));
    // } else if (arg.equals("deadzone")) {
    //     serial->println("deadzone " + String(_cfg->getDeadzone()));
    // } else if (arg.equals("flutter")) {
    //     serial->println("flutter " + String(_cfg->getFlutter()));
    // } else if (arg.equals("stickmode") || arg.equals("sm")) {
    //     serial->println("stickMode " + String((int)_cfg->getStickMode()));
    // } else {
    //     serial->println("Settings parameter not found!");
    // }
}
void Cmd::cmdSet(String arg, String val) {

    if (val.equals("")) {
        serial->println("No value parameter specified for: " + arg);
        return;
    }

    // if (arg.equals("rxmin")) {
    //     _cfg->setRxrangeMin(val.toInt());
    // } else if (arg.equals("rxmax")) {
    //     _cfg->setRxrangeMax(val.toInt());
    // } else if (arg.equals("deadzone")) {
    //     _cfg->setDeadzone(val.toInt());
    // } else if (arg.equals("flutter")) {
    //     _cfg->setFlutter(val.toInt());
    // } else if (arg.equals("stickmode") || arg.equals("sm")) {
    //     _cfg->setStickMode((bool)val.toInt());
    // } else {
    //     serial->println("Settings parameter not found!");
    // }
}
void Cmd::cmdHelp(void) {
    serial->println("Use the following commands:");
    serial->println("  help:  To view this help.");
    serial->println("  show:  To show the current settings.");
    serial->println("  get <param>: Display the value of a settings parameter.");
    serial->println("  set <param> <value>: Set the value of a settings parameter.");
    serial->println("  reset:  Factory Reset and save the settings.");
    serial->println("  read:  Read settings from eeprom.");
    //serial->println("  Cal:  Calabrate ESC`s (requires restart)");
    serial->println("  save:  Save settings to memory.");
    //serial->println("  reboot:  restart the system");
#if defined(DEBUG)
    serial->println("DEBUG Commands:");
    // undocumented commands for debugging
    serial->println("  ppmdump:  dump the ppm channels received. (requires reset to exit)");
    serial->println("  clear:  Clear the settings to their zero values.");
    serial->println("  erase:  <alias: format> Format the eeprom contents. requires reset and save to restore settings.");
#endif
    serial->println();
}
void Cmd::cmdShowCfg(void) {
    // serial->println(PROJECT_NAME + " Settings:");
    // serial->println("  version      " + String(VERSION));

//    serial->println("  rxMin        " + String((int)_cfg->getRxrangeMin()));
//    serial->println("  rxMax        " + String((int)_cfg->getRxrangeMax()));
//    //serial->println("  deadzone     " + String(_cfg->getDeadzone()));
//    serial->println("  flutter      " + String(_cfg->getFlutter()));
//    String sm = ((int)_cfg->getStickMode() == 0) ? "0 [SINGLE STICK]" : "1 [DUAL STICK]";
//    serial->println("  stickMode    " + sm);
#if defined(DEBUG)
    serial->println("--------------------------------------");
    //serial->println("  Eeprom Size  " + String(EEPROM.length()));
#endif
    serial->println();
}
void Cmd::cmdSaveCfg(void) {
//    _cfg->saveCfg();
    serial->println("Settings saved.");

}
void Cmd::cmdInitCfg(void) {
//    _cfg->resetCfg();
//    _cfg->saveCfg();
    serial->println("Settings reset and saved.");
}

String Cmd::getValue(String data, char separator, int index)
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
