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

#include "headers.h"
#include "global.h"


void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.println("\n---------------" + String(PROJECT_NAME) + "-------------");
  Serial.println("  Author:      " + String(AUTHOR));
  Serial.println("  Version:     " + String(VERSION));
  Serial.println("  Date:        " + String(BUILD_DATE));
  Serial.println("---------------------------------------------");
  Serial.println();

  BTC.setup();

}

void loop() {

  BTC.loop();

}

