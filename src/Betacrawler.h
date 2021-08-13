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
 * 
 */

#ifndef BC_BETACRAWLER_H
#define BC_BETACRAWLER_H

#include "pins/pins.h"
#include "configuration.h"
#include "Settings/Settings.h"
#include "Cmd/Cmd.h"
#include "PPMReader/PPMReader.h"
#include "Throttle/Throttle.h"
#include "Betacrawler.h"



class Betacrawler {

  public:
     Betacrawler(Settings* _settings, Cmd* _cli, PPMReader* _ppm, Throttle* _throttle);
     Betacrawler(Settings* _settings, PPMReader* _ppm, Throttle* _throttle);
     Betacrawler();
    ~Betacrawler();

    void setup(void);
    void loop(void);
    Settings* getSettings(void);
    Stream* getSerial(void);
    Cmd* getCli(void);
    PPMReader* getPPM(void);
    Throttle* getThrottle(void);    

  private:
    Stream* serial;
    Settings* settings;
    Cmd* cli = nullptr;
    PPMReader* ppm;
    Throttle* throttle;
};


//static Betacrawler BTC;
#endif    /** BC_BETACRAWLER_H **/
