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
#include "Cmd/Cmd.h"
#include "Mixer/Mixer.h"
#include "Betacrawler.h"

class Betacrawler {

  public:
     Betacrawler(Mixer* pMixer, Throttle* pThrottle, Cmd* pCli);
     Betacrawler(Mixer* pMixer, Throttle* pThrottle);
     Betacrawler();
    ~Betacrawler();

    void setup(void);
    void loop(void);
    Settings* getSettings(void);
    Stream* getSerial(void);
    Cmd* getCli(void);
    PPMReader* getPPM(void);
    Throttle* getThrottle(void);
    Mixer* getMixer(void);

  private:
    Mixer* mixer;
    Throttle* throttle;
    Cmd* cli = nullptr;
};

#endif    /** BC_BETACRAWLER_H **/