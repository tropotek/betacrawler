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
 *
 * Sources:
 *   https://www.norwegiancreations.com/2018/02/creating-a-command-line-interface-in-arduinos-serial-monitor/
 */

#ifndef TK_COMMAND_H
#define TK_COMMAND_H

#if defined(ARDUINO) && ARDUINO >= 100
  #include "Arduino.h"
#else
  #include "WProgram.h"
#endif

#include "configuration.h"
#include "Settings/Settings.h"
#include "PPMReader/PPMReader.h"
//#include <EEPROM.h>

#define MAX_NUM_ARGS 4      //Maximum number of arguments

class Command {
  public:
     Command(Settings* pCfg, PPMReader* pPppm);
    ~Command();

    void setup(void);
    void loop(void);

  private:
    Settings* _cfg;
    PPMReader* _ppm;
    String commandStr;
    String args[MAX_NUM_ARGS];
    bool cmdFinish = false;

    void parse(String cmd);
    
    void cmdHelp(void);
    void cmdSet(String arg, String val);
    void cmdGet(String arg, String val);
    void cmdShowCfg(void);
    void cmdSaveCfg(void);
    void cmdInitCfg(void);

    String getValue(String data, char separator, int index);

};

#endif    /** COMMAND_H **/
