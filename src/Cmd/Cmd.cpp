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

/*
 * Reset the board 
 */
void BC_systemReset(void) {
#if defined(BC_AVR)
    void(* resetFunc) (void) = 0; //declare reset function @ address 0
    resetFunc();
#elif defined(BC_STM32F4)
    NVIC_SystemReset();
#endif
}


Cmd::Cmd(Stream *streamObject, Settings *_settings) {
    serial = streamObject;
    settings = _settings;
    commandStr = "";
}
Cmd::Cmd(Stream *streamObject) {
    serial = streamObject;
    commandStr = "";
}
Cmd::~Cmd() { }

void Cmd::setup() {
    getSerial()->print("> ");
}

void Cmd::loop() {
    char c;
    while(getSerial()->available()) {
        c = getSerial()->read();
        if (c == '\n') {
            cmdFinish = true;
        } else if (c == '\r' || c == '\t' || c == '\0') {
            ; // ingnore
        } else {
            commandStr += c;
            getSerial()->print(c);
        }
    }
    if (!commandStr.equals("") && cmdFinish) {
        getSerial()->println();
        parse(commandStr);
        cmdFinish = false;
        commandStr = "";
        getSerial()->print("> ");
    }
}

Settings* Cmd::getSettings(void) {
    return settings;
}

Stream* Cmd::getSerial(void) {
    return serial;
}

void Cmd::parse(String cmdStr) {
    for (int i = 0; i < MAX_NUM_ARGS; i++) {
        args[i] = "";  // reset cmd array
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

    if (args[0].equals("help") || args[0].equals("h") || args[0].equals("?")) {
        cmdHelp();
    } else if (args[0].equals("show") || args[0].equals("list") || args[0].equals("l")) {
        cmdShowCfg();
    } else if (args[0].equals("save")) {
        cmdSaveCfg();
    } else if (args[0].equals("reset")) {
        cmdInitCfg();
    } else if (args[0].equals("read")) {
        getSettings()->readCfg();
        getSerial()->println("Settings reloaded from eeprom.");
    } else if (args[0].equals("reboot")) {
        getSerial()->println("Rebooting....");
        BC_systemReset();
    } else if (args[0].equals("get")) {
        cmdGet(args[1], args[2]);
    } else if (args[0].equals("set")) {
        cmdSet(args[1], args[2]);
    } 
    else {
        getSerial()->println("Command not found!");
    }

}

void Cmd::cmdGet(String arg, String val) {
    getSerial()->println("---- Not implemented ----");
    
    if (arg.equals("flutter")) {
        getSerial()->println("flutter: " + String(getSettings()->getFlutter()));
    } 
    else if (arg.equals("deadzone")) {
        getSerial()->println("deadzone: " + String(getSettings()->getDeadzone()));
    }
    else if (arg.equals("expo")) {
        getSerial()->println("expo: " + String(getSettings()->getExpo()));
    }
    else if (arg.equals("tlimit")) {
        getSerial()->println("tlimit: " + String(getSettings()->getExpo()));
    }
    else if (arg.equals("txmap")) {
        getSerial()->println("txmap: " + String(getSettings()->getTxMap()));
    }
    else if (arg.equals("txmode")) {
        getSerial()->println("txmode: " + String(getSettings()->getTxMode()));
    }
    else if (arg.equals("reverse")) {
        String rev = getSettings()->hasReverse() ? "On" : "Off";
        getSerial()->println("reverse: " + rev);
    }
    else {
        getSerial()->println("Settings parameter not found!");
    }
}

void Cmd::cmdSet(String arg, String val) {

    if (val.equals("")) {
        getSerial()->println("No value parameter specified for: " + arg);
        return;
    }
    
    val.toLowerCase();
    if (arg.equals("flutter")) {
        getSettings()->setFlutter(val.toInt());
    }
    else if (arg.equals("deadzone")) {
        getSettings()->setDeadzone(val.toInt());
    }
    else if (arg.equals("expo")) {
        getSettings()->setExpo(val.toInt());
    }
    else if (arg.equals("tlimit")) {
        getSettings()->setThrottleLimit(val.toInt());
    }
    else if (arg.equals("txmode")) {
        getSettings()->setTxMode(val.toInt());
    }
    else if (arg.equals("reverse")) {
        bool b = (val.equals("on"));
        getSettings()->enableReverse(b);
    }
    else if (arg.equals("txmap")) {
        val.toUpperCase();
        char c[10];
        val.toCharArray(c, 10);
        getSettings()->setTxMap(c);
    }
    else {
        getSerial()->println("Settings parameter not found!");
    }
}

void Cmd::cmdHelp(void) {
    getSerial()->println("Use the following commands:");
    getSerial()->println("  help:  To view this help.");
    getSerial()->println("  show:  To show the current settings.");
    getSerial()->println("  get <param>: Display the value of a settings parameter.");
    getSerial()->println("  set <param> <value>: Set the value of a settings parameter.");
    getSerial()->println("  reset:  Factory Reset and save the settings.");
    //getSerial()->println("  read:  Read settings from eeprom.");
    getSerial()->println("  save:  Save settings to memory.");
    getSerial()->println("  reboot:  restart the system");
#if defined(DEBUG)
    getSerial()->println("DEBUG Commands:");
    // undocumented commands for debugging
    getSerial()->println("  ppmdump:  dump the ppm channels received. (requires reset to exit)");
    //getSerial()->println("  clear:  Clear the settings to their zero values.");
    //getSerial()->println("  erase:  <alias: format> Format the eeprom contents. requires reset and save to restore settings.");
#endif
    getSerial()->println();
}

void Cmd::cmdShowCfg(void) {
    getSerial()->println(getSettings()->toString());
    getSerial()->println();
}

void Cmd::cmdSaveCfg(void) {
    getSettings()->saveCfg();
    getSerial()->println("Settings saved.");
}

void Cmd::cmdInitCfg(void) {
    getSettings()->resetCfg();
    getSettings()->saveCfg();
    getSerial()->println("Settings reset and saved.");
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
