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

Betacrawler::Betacrawler(Mixer* pMixer, Cmd* pCli) {
    mixer = pMixer;
    cli = pCli;
}
Betacrawler::Betacrawler(Mixer* pMixer) {
    mixer = pMixer;
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

    getThrottle()->loop();



    if (tick%500 == 0) {  // every 1/2 a second

  //   unsigned arm = getPPM()->latestValidChannelValue(7, 0);
  //   if (arm > 1200) {
  //     unsigned speed = getPPM()->latestValidChannelValue(1, 0);
  //     getThrottle()->speed(speed);
  //     getThrottle()->arm();
  //   } else {
  //     getThrottle()->disarm();
  //   }

    
    // // Print latest valid values from all channels
    Serial.print("                                                               \r");
    for (byte channel = 1; channel <= MAX_RX_CHANNELS; ++channel) {
        unsigned value = getPPM()->latestValidChannelValue(channel, 0);
        Serial.print(String(value) + "\t");
    }
    Serial.print("\r");

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
    return getMixer()->getThrottle();
}
