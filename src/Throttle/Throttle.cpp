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
#include "Throttle.h"


Throttle::Throttle(int esc0Pin, int esc1Pin) {
    _esc0Pin = esc0Pin;
    _esc1Pin = esc1Pin;
}

Throttle::~Throttle() { }

void Throttle::setup(void) {
    _leftEsc  = new ESC(_esc0Pin, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE, ESC_ARM);
    _rightEsc = new ESC(_esc1Pin, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE, ESC_ARM);
    
    // getLeftEsc()->setStopPulse(0);
    // getRightEsc()->setStopPulse(0);
    // if (hasReverse()) {
    //     getLeftEsc()->setStopPulse(ESC_MID_THROTTLE);
    //     getRightEsc()->setStopPulse(ESC_MID_THROTTLE);
    // } else {
    //     getLeftEsc()->setStopPulse(ESC_MIN_THROTTLE);
    //     getRightEsc()->setStopPulse(ESC_MIN_THROTTLE);
    // }
    getLeftEsc()->arm();
    getRightEsc()->arm();
    delay(2000);
    speed(ESC_MAX_THROTTLE);
    delay(2000);
    stop();
    delay(100);

}

void Throttle::loop(void) { 
    
    if (isArmed()) {
        _leftEsc->speed(getLeftSpeed());
        _rightEsc->speed(getRightSpeed());
    } else {
        
    }

}

void Throttle::arm(bool b) {
    if (b) {
        if (isArmed()) return;
        digitalWrite(LED_PIN, LOW);             // LED ON Once Armed
    } else {
        if (!isArmed()) return;
        stop();
        digitalWrite(LED_PIN, HIGH);            // LED OFF Once Armed
    }
    _armed = b;
}

void Throttle::stop(void) {
    if (hasReverse()) {
        speed(ESC_MID_THROTTLE);
    } else {
        speed(ESC_MIN_THROTTLE);
    }
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
    _leftEsc->speed(getLeftSpeed());
    _rightEsc->speed(getRightSpeed());
}
void Throttle::enableReverse(bool b)
{
    reverse = b;
}
bool Throttle::hasReverse(void)
{
    return reverse;
}

String Throttle::toString(void) {
    String str = "";
    str += String(getLeftSpeed()) + " ";
    str += String(getRightSpeed()) + " ";
    String rev = hasReverse() ? "Enabled" : "Disabled";
    str += rev;
    return str;
}
