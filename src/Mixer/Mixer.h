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
 */

#ifndef BC_MIXER_H
#define BC_MIXER_H

#include "configuration.h"
#include "Settings/Settings.h"
#include "PPMReader/PPMReader.h"
#include "Throttle/Throttle.h"

// #include "Esc/Esc.h"
// #include <Servo.h>


class Mixer {
  public:
     Mixer(Settings* pSettings, PPMReader* pPpm, Throttle* pThrottle);
    ~Mixer();

    void setup(void);
    void loop(void);
    
    Settings* getSettings(void);
    PPMReader* getPpm(void);
    Throttle* getThrottle(void);
    
  private:
    Throttle* throttle;
    Settings* settings;
    PPMReader* ppm;
    
};

#endif  /* BC_MIXER_H */