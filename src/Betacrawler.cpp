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

Betacrawler::Betacrawler(Settings* _settings, Cmd* _cli, PPMReader* _ppm, Throttle* _throttle) {
    settings = _settings;
    cli = _cli;
    ppm = _ppm;
    throttle = _throttle;
}
Betacrawler::Betacrawler(Settings* _settings, PPMReader* _ppm, Throttle* _throttle) {

    settings = _settings;
    ppm = _ppm;
    throttle = _throttle;
    
}
Betacrawler::Betacrawler() { }
Betacrawler::~Betacrawler() { }


void Betacrawler::setup(void) {
  
  pinMode(LED_PIN, OUTPUT);
  //pinMode(BTN_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, HIGH);
  getThrottle()->setup();

}

void Betacrawler::loop(void) {

  //Serial.println(settings.toString());

    if (getCli() != nullptr)
        getCli()->loop();

  getThrottle()->loop();

  // if (millis()%500 == 0) {  // every 1/2 a second

  //   unsigned arm = ppm.latestValidChannelValue(7, 0);
  //   if (arm > 1200) {
  //     unsigned speed = ppm.latestValidChannelValue(1, 0);
  //     throttle.speed(speed);
  //     throttle.arm();
  //   } else {
  //     throttle.disarm();
  //   }

    
    // // Print latest valid values from all channels
    // for (byte channel = 1; channel <= MAX_RX_CHANNELS; ++channel) {
    //     unsigned value = ppm.latestValidChannelValue(channel, 0);
    //     Serial.print(String(value) + "\t");
    // }
    // Serial.println();

   // }
}



Settings* Betacrawler::getSettings(void) {
    return settings;
}
Stream* Betacrawler::getSerial(void) {
    return serial;
}
Cmd* Betacrawler::getCli(void) {
    return cli;
}
PPMReader* Betacrawler::getPPM(void) {
    return ppm;
}
Throttle* Betacrawler::getThrottle(void) {
    return throttle;
}




