/*
 * Copyright (C) 2020  www.tropotek.com
 * Project: Betacrawler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "Mixer.h"


Mixer::Mixer(Settings* pCfg, PPMReader* pPpm) {
    _cfg = pCfg;
    _ppm = pPpm;
}
Mixer::~Mixer() { }

void Mixer::setup(void) { 

#if !defined(SERVO_LIB)
    _leftEsc  = new Esc((int)ESC0_PIN);
    _rightEsc = new Esc((int)ESC1_PIN);
#else
    _leftEsc  = new Servo();
    _rightEsc  = new Servo();
    _leftEsc->attach(ESC0_PIN);
    _rightEsc->attach(ESC1_PIN);
#endif

    armSvo();
}
void Mixer::loop(void) { 
    // Read the current arm switch state 
   arm(_ppm->latestValidChannelValue(CH_AUX1, 1001) >= 1800 && _ppm->latestValidChannelValue(CH_AUX1, 1001) <= 2100);
    // Read the stick control values and calculate the left right esc pulses
    if (isArmed()) {
        if (_cfg->getStickMode() == MODE_DUAL) {
            // Start here
            _leftSpeed = scaleSpeed(_ppm->latestValidChannelValue(CH_THROT_LEFT, 1001));
            _rightSpeed = scaleSpeed(_ppm->latestValidChannelValue(CH_THROT_RIGHT, 1001));
         
        } else if (_cfg->getStickMode() == MODE_SINGLE) {
            _leftSpeed = scaleSpeed(_ppm->latestValidChannelValue(CH_THROT, 100));
            _rightSpeed = scaleSpeed(_ppm->latestValidChannelValue(CH_DIR, 1001));
            // TODO: need to build an algorithm with some smarts here
        } else {
#if !defined(SERVO_LIB)
            // _leftEsc->speed(1001);
            // _rightEsc->speed(1001);
            _leftEsc->stop();
            _rightEsc->stop();
#else
            _leftEsc->writeMicroseconds(500);
            _rightEsc->writeMicroseconds(500);
#endif
        }
        writeEscSpeed();
    }
}
void Mixer::writeEscSpeed(void) {
    if (isDeadzone(_leftSpeed)) _leftSpeed = TK_MID_THROTTLE;
    if (isDeadzone(_rightSpeed)) _rightSpeed = TK_MID_THROTTLE;

#if !defined(SERVO_LIB)
    int curLeftSpeed = _leftEsc->getSpeed();
    int curRightSpeed = _rightEsc->getSpeed();
#else
    int curLeftSpeed = _leftEsc->readMicroseconds();
    int curRightSpeed = _rightEsc->readMicroseconds();
#endif
 
    if (!(curLeftSpeed >= (_leftSpeed - _cfg->getFlutter()) && curLeftSpeed <= (_leftSpeed + _cfg->getFlutter())))  {
        if (_leftSpeed != curLeftSpeed) {
            //Serial.println("ESC_LEFT: " + String(_leftSpeed) + " [" + String(curLeftSpeed) + "]");
        }
#if !defined(SERVO_LIB)
        _leftEsc->speed(_leftSpeed);
#else
        _leftEsc->writeMicroseconds(_leftSpeed);
#endif
    }
    if (!(curRightSpeed >= (_rightSpeed - _cfg->getFlutter()) && curRightSpeed <= (_rightSpeed + _cfg->getFlutter()))) {
        if (_rightSpeed != curRightSpeed) {
            //Serial.println("ESC_RIGHT: " + String(_rightSpeed) + " [" + String(curRightSpeed) + "]");
        }
#if !defined(SERVO_LIB)
        _rightEsc->speed(_rightSpeed);
#else
        _rightEsc->writeMicroseconds(_rightSpeed);
#endif
    }
}

int Mixer::scaleSpeed(int rxVal) { 
    //return map(rxVal, _cfg->getRxrangeMin(), _cfg->getRxrangeMax(), TK_MIN_THROTTLE, TK_MAX_THROTTLE);
    return 1000 * (((double)rxVal - _cfg->getRxrangeMin()) / (_cfg->getRxrangeMax() - _cfg->getRxrangeMin())) + 1000;
}

bool Mixer::isDeadzone(int speed)
{
    if (speed >= (TK_MID_THROTTLE - _cfg->getDeadzone()) && speed <= (TK_MID_THROTTLE + _cfg->getDeadzone()))
        return true;
    return false;
}

void Mixer::arm(bool b) {
    if (b && !isArmed()) {
        // trigger Arming
        Serial.println("Tank Armed!");
    } else if(!b && isArmed()) {
        // Trigger dissarm
        // _leftEsc->stop();
        // _rightExc->stop();
        Serial.println("Tank Disarmed!");
    }
    _armed = b;
}
bool Mixer::isArmed(void) {
    return _armed;
}


void Mixer::armSvo(void) {
#if !defined(SERVO_LIB)
    //Serial.println("Arming Left ESC");
    _leftEsc->arm();
    //Serial.println("Arming Right ESC");
    _rightEsc->arm();
#else 
    //Serial.println("Arming Left ESC");
	_leftEsc->writeMicroseconds(1000);
    //Serial.println("Arming Right ESC");
    _rightEsc->writeMicroseconds(1000);
#endif
}
