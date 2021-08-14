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
 * Sources:
 *   https://www.norwegiancreations.com/2018/02/creating-a-command-line-interface-in-arduinos-serial-monitor/
 */

#ifndef BC_PAN_H
#define BC_PAN_H

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif
#include "configuration.h"
#include <Servo.h>


class Pan {
  public:
     Pan(int panPin, int tiltPin);
     Pan(int panPin);
    ~Pan();

    void setup(void);
    void loop(void);

    uint16_t getPan(void);
    uint16_t getTilt(void);
    uint16_t getPrevPan(void);
    uint16_t getPrevTilt(void);
    int getFlutter(void);
    void setPan(int i);
    void setTilt(int i);
    void setFlutter(int i);

  private:
    int _flutter = 10;
    int _panPin = 0;
    int _tiltPin = 0;
    uint16_t _prevPan = 0;
    uint16_t _prevTilt = 0;
    uint16_t _pan = 0;
    uint16_t _tilt = 0;
    Servo* _panServo = nullptr;
    Servo* _tiltServo = nullptr;

};

#endif    /** BC_CMD_H **/


