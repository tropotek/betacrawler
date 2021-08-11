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
  
  //SerialUSB.begin(SERIAL_BAUD);
  Serial.begin(SERIAL_BAUD);
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, HIGH);

  throttle.setup();

  // Serial.println("\n---------------" + String(PROJECT_NAME) + "-------------");
  // Serial.println("  Author:      " + String(AUTHOR));
  // Serial.println("  Version:     " + String(VERSION));
  // Serial.println("  Date:        " + String(BUILD_DATE));
  // Serial.println("---------------------------------------------");
  // Serial.println();
}

void loop() {

#ifdef BC_CLI
  cmd.loop();
#endif

  throttle.loop();

  if (millis()%500 == 0) {  // every 1/2 a second

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
  

}

