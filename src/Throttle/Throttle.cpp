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
#include "Throttle.h"


Throttle::Throttle(int esc0Pin, int esc1Pin) {
    _esc0Pin = esc0Pin;
    _esc1Pin = esc1Pin;
}

Throttle::~Throttle() { }

void Throttle::setup(void) { 
    _leftEsc  = new ESC(_esc0Pin, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE, ESC_ARM);
    _rightEsc = new ESC(_esc1Pin, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE, ESC_ARM);
    _leftEsc->arm();
    _rightEsc->arm();
    delay(5000);                            // Wait for a while
    
}

void Throttle::loop(void) { 
    
    if (isArmed()) {
        _leftEsc->speed(getLeftSpeed());
        _rightEsc->speed(getRightSpeed());
    } else {
        // _leftEsc->stop();
        // _rightEsc->stop();
    }

}

void Throttle::arm(void) {
    if (isArmed()) return;
    digitalWrite(LED_PIN, LOW);             // LED ON Once Armed
    _armed = true;
}
void Throttle::disarm(void) {
    if (!isArmed()) return;
    _leftEsc->stop();
    _rightEsc->stop();
    digitalWrite(LED_PIN, HIGH);            // LED OFF Once Armed
    //delay(1000);                            // Wait for a while
    _armed = false;
}
void Throttle::calib(void) {
    if (isArmed()) disarm();
    _leftEsc->calib();
    _rightEsc->calib();
}


bool Throttle::isArmed(void) {
    return _armed;
}
int Throttle::getLeftSpeed(void) {
    return _leftSpeed;
}
int Throttle::getRightSpeed(void) {
    return _rightSpeed;
}
ESC* Throttle::getLeftEsc(void) {
    return _leftEsc;
}
ESC* Throttle::getRightEsc(void) {
    return _rightEsc;
}
void Throttle::setLeftSpeed(int i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    _leftSpeed = i;
}
void Throttle::setRightSpeed(int i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    _rightSpeed = i;
}
void Throttle::speed(int i) {
    setLeftSpeed(i);
    setRightSpeed(i);
}

