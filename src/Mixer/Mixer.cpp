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
#include "Mixer.h"


Mixer::Mixer(Settings* pSettings, PPMReader* pPpm, Throttle* pThrottle) {
    
    settings = pSettings;
    ppm = pPpm;
    throttle = pThrottle;
}
Mixer::~Mixer() { }

void Mixer::setup(void) { 
    
    
}
void Mixer::loop(void) { 
    // // Failsafe
    // if (getPpm()->timeSinceLastPulse() > SIGNAL_LOSS_TIME) {
    //     // shutdown (no paulses)
    //     if(isArmed())
    //         Serial.println("Signal Loss. (Failsafe)");
    //     arm(false);
    //     return;
    // }

}



Settings* Mixer::getSettings(void) {
    return settings;
}
PPMReader* Mixer::getPpm(void) {
    return ppm;
}
Throttle* Mixer::getThrottle(void) {
    return throttle;
}
