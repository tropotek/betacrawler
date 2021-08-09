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

#ifndef TK_CLI_H
#define TK_CLI_H

#if defined(ARDUINO) && ARDUINO >= 100
  #include "Arduino.h"
#else
  #include "WProgram.h"
#endif

//#include "headers.h"

#define MAX_NUM_ARGS 4      //Maximum number of arguments

class Cli {
  public:
     Cli(Stream *streamObject);
    ~Cli();

    void setup(void);
    void loop(void);

  private:
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

#endif    /** CLI_H **/
