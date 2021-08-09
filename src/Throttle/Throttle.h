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
 * Define any global objects here an include all libs and headers
 * 
 */

#ifndef TK_THROTTLE_H
#define TK_THROTTLE_H

#include "headers.h"
#include <Esc.h>


class Throttle {
  public:
     Throttle(int esc0Pin, int esc1Pin);
    ~Throttle();

    void setup(void);
    void loop(void);

    void setLeftSpeed(int i);
    void setRightSpeed(int i);
    int getLeftSpeed(void);
    int getRightSpeed(void);

    ESC* getLeftEsc(void);
    ESC* getRightEsc(void);

    void arm(void);
    void disarm(void);
    void calib(void);
    bool isArmed(void);
    void speed(int i);
    
  private:
    int _esc0Pin = 0;
    int _esc1Pin = 0;

    bool _armed = false;
    int _leftSpeed = 0;
    int _rightSpeed = 0;

    ESC* _leftEsc;
    ESC* _rightEsc;
    
    
};

#endif  /* TK_THROTTLE_H */