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
#include "Pan.h"


Pan::Pan(int panPin, int tiltPin) {
    _panPin = panPin;
    _tiltPin = tiltPin;
    _panServo = new Servo();
    _tiltServo = new Servo();
    
}
Pan::Pan(int panPin) {
    _panPin = panPin;
    _panServo = new Servo();
}

Pan::~Pan() { }

void Pan::setup(void) {
    _panServo->attach(_panPin);
    if (_tiltServo != nullptr)
        _tiltServo->attach(_tiltPin);
    
}

void Pan::loop(void) { 

    if (_panServo->attached()) {
        if (!(getPan() >= (getPrevPan() - getFlutter()) && getPan() <= (getPrevPan() + getFlutter())))  {
            int pAngle = map(getPrevPan(), ESC_MIN_THROTTLE, ESC_MAX_THROTTLE, BC_MIN_ANGLE, BC_MAX_ANGLE);
            int angle = map(getPan(), ESC_MIN_THROTTLE, ESC_MAX_THROTTLE, BC_MIN_ANGLE, BC_MAX_ANGLE);
            if (pAngle != angle) {
                _panServo->write(angle);
                _prevPan = getPan();
                //delay(10);   // waits 10ms for the servo to reach the position
            }
        }
    }
    if (_tiltServo != nullptr && _tiltServo->attached()) {
        if (!(getTilt() >= (getPrevTilt() - getFlutter()) && getTilt() <= (getPrevTilt() + getFlutter())))  {
            int pAngle = map(getPrevTilt(), ESC_MIN_THROTTLE, ESC_MAX_THROTTLE, BC_MIN_ANGLE, BC_MAX_ANGLE);
            int angle = map(getTilt(), ESC_MIN_THROTTLE, ESC_MAX_THROTTLE, BC_MIN_ANGLE, BC_MAX_ANGLE);
            if (pAngle != angle) {
                _tiltServo->write(angle);
                _prevTilt = getTilt();
                //delay(10);   // waits 10ms for the servo to reach the position
            }
        }
    }
    
}


int Pan::getFlutter(void) {
    return _flutter;
}
uint16_t Pan::getPan(void) {
    return _pan;
}
uint16_t Pan::getTilt(void) {
    return _tilt;
}
uint16_t Pan::getPrevPan(void) {
    return _prevPan;
}
uint16_t Pan::getPrevTilt(void) {
    return _prevTilt;
}

void Pan::setPan(int i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    _pan = i;
}
void Pan::setTilt(int i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    _tilt = i;
}
void Pan::setFlutter(int i) {
    i = constrain(i, 0, 100);
    _flutter = i;
}


