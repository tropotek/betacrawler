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
#include "Betacrawler.h"

Betacrawler::Betacrawler(Mixer* pMixer, Throttle* pThrottle, Cmd* pCli) {
    mixer = pMixer;
    throttle = pThrottle;
    cli = pCli;
}
Betacrawler::Betacrawler(Mixer* pMixer, Throttle* pThrottle) {
    mixer = pMixer;
    throttle = pThrottle;
}
Betacrawler::Betacrawler() { }
Betacrawler::~Betacrawler() { }


void Betacrawler::setup(void) {
  
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    getThrottle()->setup();

}

void Betacrawler::loop(void) {
    static uint16_t tick = 0;
    tick++;
    
    if (getCli() != nullptr)
        getCli()->loop();

    // Read values from the PPM receiver and map channel values
    getMixer()->loop();


    // Arm/Disarm the throttle
    getThrottle()->arm((getMixer()->getArm() > 1250));
    // Set the Esc throrrle values
    getThrottle()->setLeftSpeed(getMixer()->getLeft());
    getThrottle()->setRightSpeed(getMixer()->getRight());
    // transmit Esc throttle values
    getThrottle()->loop();

    // TODO: Send values to the Pan/Tilt camera servos



    // TODO: Write code here to perform functions to use Aux2, Aux3, Aux4
    


    if (tick%1000 == 0) {  // every 1/2 a second Good place to output debug data
    
        // getSerial()->print("                                                                     \r");
        // getSerial()->print(getMixer()->toString());
        // getSerial()->print("\r");

   }
}



Mixer* Betacrawler::getMixer(void) {
    return mixer;
}
Cmd* Betacrawler::getCli(void) {
    return cli;
}

Settings* Betacrawler::getSettings(void) {
    return getMixer()->getSettings();
}
Stream* Betacrawler::getSerial(void) {
    return &Serial; // TODO: not sure if this is a good Idea, maybe we should pass it through from the constructor???
}
PPMReader* Betacrawler::getPPM(void) {
    return getMixer()->getPpm();
}
Throttle* Betacrawler::getThrottle(void) {
    return throttle;
}
