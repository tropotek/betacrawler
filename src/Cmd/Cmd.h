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
 */

#ifndef BC_CMD_H
#define BC_CMD_H

#include "headers.h"


#define MAX_NUM_ARGS 4      //Maximum number of arguments

class Cmd {
  public:
     Cmd(Stream *streamObject, Settings *settings);
     Cmd(Stream *streamObject);
    ~Cmd();

    void setup(void);
    void loop(void);
    Settings* getSettings(void);
    Stream* getSerial(void);

  private:
    Settings* settings;
    Stream* serial;
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

#endif    /** BC_CMD_H **/
