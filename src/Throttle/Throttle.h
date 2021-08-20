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
 * Resources
 *   PPM Reciver: https://github.com/Nikkilae/PPM-reader
 *   ESC/Servo: https://www.instructables.com/id/ESC-Programming-on-Arduino-Hobbyking-ESC/
 *   ESC bi-directional:  https://www.youtube.com/watch?v=jBr-ZLMt4W4
 *   ESC: https://github.com/MikeysLab/BrushlessESCviaPWM/blob/master/EscPWMTesting/EscPWMTesting.ino
 *   ESC: https://www.robotshop.com/community/blog/show/rc-speed-controller-esc-arduino-library
 *
 * ESC's used in this project: HGLRC BS30A - BLHli_S: Rev16.6 F-H-40
 * 
 */

#ifndef BC_THROTTLE_H
#define BC_THROTTLE_H

#include "pins/pins.h"
#include "configuration.h"
#include <Esc.h>


class Throttle {
  public:
     Throttle(int esc0Pin, int esc1Pin);
    ~Throttle();

    void setup(void);
    void loop(void);

    void enableReverse(bool b);
    void setLeftSpeed(int i);
    void setRightSpeed(int i);
    int getLeftSpeed(void);
    int getRightSpeed(void);

    ESC* getLeftEsc(void);
    ESC* getRightEsc(void);

    bool hasReverse(void);
    void arm(bool b);
    bool isArmed(void);
    void speed(int i);
    void stop(void);
    
    String toString(void);
    
  private:
    int _esc0Pin = 0;
    int _esc1Pin = 0;

    bool _armed = false;
    int _leftSpeed = 0;
    int _rightSpeed = 0;
    bool reverse = false;

    ESC* _leftEsc;
    ESC* _rightEsc;
    
    
};

#endif  /* BC_THROTTLE_H */