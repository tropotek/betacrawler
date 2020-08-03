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
    pinMode(LED_PIN, OUTPUT); 
    digitalWrite(LED_PIN, LOW);
    _leftEsc  = new Esc((int)ESC0_PIN);
    _rightEsc = new Esc((int)ESC1_PIN);
    armSvo();
}
void Mixer::loop(void) { 
    
    
    // Read the current arm switch state 
    // WARNING: When the TX transmitter is powered off this can be set to armed and full throttle
    //          TODO: We need to check for if the throttle is > TK_MIN_THROTTLE then do not ar, throw an error msg
    arm(_ppm->latestValidChannelValue(CH_AUX1, TK_MIN_THROTTLE) >= 1800 && _ppm->latestValidChannelValue(CH_AUX1, TK_MIN_THROTTLE) <= 2100);


    // Read the stick control values and calculate the left right esc pulses
    if (_cfg->getStickMode() == MODE_DUAL) {
        // Start here
        _leftSpeed = scaleSpeed(_ppm->latestValidChannelValue(CH_THROT_LEFT, TK_MIN_THROTTLE));
        _rightSpeed = scaleSpeed(_ppm->latestValidChannelValue(CH_THROT_RIGHT, TK_MIN_THROTTLE));
        
    } else if (_cfg->getStickMode() == MODE_SINGLE) {
        int throt = scaleSpeed(_ppm->latestValidChannelValue(CH_THROT, TK_MIN_THROTTLE));
        // 1000 = left | 1500 = straight | 2000 = right
        int dir = scaleSpeed(_ppm->latestValidChannelValue(CH_DIR, TK_MIN_THROTTLE));
        int leftSub = 0;
        int rightSub = 0;

        // int lThrowVal = TK_MID_THROTTLE - TK_MAX_THROTTLE;          // 500 is the left and right movment amount
        // int rThrowVal = TK_MIN_THROTTLE - TK_MID_THROTTLE;          // 500 is the left and right movment amount

        // We subtract speed from the opposite motor to turn
        if (dir > TK_MID_THROTTLE) {    // left stick
            rightSub = dir-TK_MID_THROTTLE;
        } else if (dir < TK_MID_THROTTLE) { // Right Stick
            leftSub = TK_MID_THROTTLE-dir;
        }
        int l = constrain(throt-rightSub, TK_MIN_THROTTLE, TK_MAX_THROTTLE);
        int r = constrain(throt-leftSub, TK_MIN_THROTTLE, TK_MAX_THROTTLE);
        _leftSpeed = l;
        _rightSpeed = r;
    } else {
        _leftSpeed = 0;
        _rightSpeed = 0;
    }

    if (isArmed()) {
        //Serial.println("ls: " + String(_leftSpeed) + "   rs: " + String(_rightSpeed));
        writeEscSpeed();
    } else {
        // TODO: This seems to stop the escs from arming and moving
        _leftSpeed = 0;
        _rightSpeed = 0;
        writeEscSpeed();
    }
}
void Mixer::writeEscSpeed(void) {
    if (isDeadzone(_leftSpeed)) _leftSpeed = TK_MID_THROTTLE;
    if (isDeadzone(_rightSpeed)) _rightSpeed = TK_MID_THROTTLE;

    int curLeftSpeed = _leftEsc->getSpeed();
    int curRightSpeed = _rightEsc->getSpeed();
 
    if (_leftSpeed <= 0) {
        _leftEsc->stop();
    } else if (!(curLeftSpeed >= (_leftSpeed - _cfg->getFlutter()) && curLeftSpeed <= (_leftSpeed + _cfg->getFlutter())))  {
        if (_leftSpeed != curLeftSpeed && isArmed()) {
            Serial.println("ESC_LEFT: " + String(_leftSpeed) + " [" + String(curLeftSpeed) + "]");
        }
        _leftEsc->speed(_leftSpeed);
    }

    if (_rightSpeed <= 0) {
        _rightEsc->stop();
    } else if (_rightSpeed > 0 && !(curRightSpeed >= (_rightSpeed - _cfg->getFlutter()) && curRightSpeed <= (_rightSpeed + _cfg->getFlutter()))) {
        if (_rightSpeed != curRightSpeed && isArmed()) {
            Serial.println("ESC_RIGHT: " + String(_rightSpeed) + " [" + String(curRightSpeed) + "]");
        }
        _rightEsc->speed(_rightSpeed);
    }
    delay(10); 
}

void Mixer::arm(bool b) {
    if (b && !isArmed()) {
        // trigger Arming
        digitalWrite(LED_PIN, HIGH);
        Serial.println("Tank Armed!");
    } else if(!b && isArmed()) {
        // Trigger dissarm and stop motors immediatly
        _leftSpeed = 0;
        _rightSpeed = 0;
        writeEscSpeed();
        digitalWrite(LED_PIN, LOW);
        Serial.println("Tank Disarmed!");
    }
    _armed = b;
}
bool Mixer::isArmed(void) {
    return _armed;
}

void Mixer::armSvo(void) {
    _leftEsc->arm();
    _rightEsc->arm();
    delay(10); 
}

int Mixer::scaleSpeed(int rxVal) { 
    rxVal = constrain(rxVal, _cfg->getRxrangeMin(), _cfg->getRxrangeMax());
    return map(rxVal, _cfg->getRxrangeMin(), _cfg->getRxrangeMax(), TK_MIN_THROTTLE, TK_MAX_THROTTLE);
    //return 1000 * (((double)rxVal - _cfg->getRxrangeMin()) / (_cfg->getRxrangeMax() - _cfg->getRxrangeMin())) + 1000;
}

bool Mixer::isDeadzone(int speed)
{
    if (speed >= (TK_MID_THROTTLE - _cfg->getDeadzone()) && speed <= (TK_MID_THROTTLE + _cfg->getDeadzone()))
        return true;
    return false;
}
