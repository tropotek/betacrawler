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

#include "global.h"


void setup() {

  Serial.begin(SERIAL_BAUD);
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, HIGH);

  throttle.setup();

  // throttle.setLeftSpeed(1080);
  // throttle.setRightSpeed(1080);

  // for(int i = 0; i < 10; i++) {
  //   if (i%2 == 0) {
  //     throttle.arm();
  //   } else {
  //     throttle.disarm();
  //   }
  //   throttle.loop();
  //   delay(5000);
  // }


}
long mili = 0;
void loop() {


  cli.loop();
  // mixer.loop();
  throttle.loop();

  if (mili%500 == 0) {
    unsigned arm = ppm.latestValidChannelValue(7, 0);
    if (arm > 1200) {
      unsigned speed = ppm.latestValidChannelValue(1, 0);
      throttle.speed(speed);
      throttle.arm();
    } else {
      throttle.disarm();
    }
    // // Print latest valid values from all channels
    // for (byte channel = 1; channel <= MAX_RX_CHANNELS; ++channel) {
    //     unsigned value = ppm.latestValidChannelValue(channel, 0);
    //     Serial.print(String(value) + "\t");
    // }
    // Serial.println();
  }
  
  
  mili++;
}

