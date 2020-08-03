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
    arm(getPpm()->latestValidChannelValue(CH_AUX1, TK_MIN_THROTTLE) >= 1800 && getPpm()->latestValidChannelValue(CH_AUX1, TK_MIN_THROTTLE) <= 2100);

    // Read the stick control values and calculate the left right esc pulses
    if (getSettings()->getStickMode() == MODE_DUAL) {
        // Start here
        setLeftSpeed(scaleSpeed(getPpm()->latestValidChannelValue(CH_THROT_LEFT, TK_MIN_THROTTLE)));
        setRightSpeed(scaleSpeed(getPpm()->latestValidChannelValue(CH_THROT_RIGHT, TK_MIN_THROTTLE)));
    } else if (getSettings()->getStickMode() == MODE_SINGLE) {
        int throt = scaleSpeed(getPpm()->latestValidChannelValue(CH_THROT, TK_MIN_THROTTLE));
        int dir = scaleSpeed(getPpm()->latestValidChannelValue(CH_DIR, TK_MIN_THROTTLE));
        int leftSub = 0;
        int rightSub = 0;
        float pct = 0.0;
        setLeftSpeed(throt);
        setRightSpeed(throt);
        // Here we will find the percentage of left/right movement and subtract that from the current throttle
        // This should result in more subttle turns of the motors
        // TODO: implement a scaling factor like rate curve for turning harder or softer.
        if (dir > TK_MID_THROTTLE) {    // left stick
            rightSub = dir-TK_MID_THROTTLE;
            pct = (float)1.0-(rightSub/500.0);
            setRightSpeed((pct*(float)(throt-1000))+1000);
        } else if (dir < TK_MID_THROTTLE) { // Right Stick
            leftSub = TK_MID_THROTTLE-dir;
            pct = (float)1.0-leftSub/500.0;
            setLeftSpeed((pct*(float)(throt-1000))+1000);
        }
    } else {
        setLeftSpeed(0);
        setRightSpeed(0);
    }

    if (isArmed()) {
        writeEscSpeed();
    } else {
        setLeftSpeed(0);
        setRightSpeed(0);
        writeEscSpeed();
    }
}
void Mixer::writeEscSpeed(void) {
    if (isDeadzone(getLeftSpeed())) setLeftSpeed(TK_MID_THROTTLE);
    if (isDeadzone(getRightSpeed())) setRightSpeed(TK_MID_THROTTLE);

    int curLeftSpeed = getLeftEsc()->getSpeed();
    int curRightSpeed = getRightEsc()->getSpeed();
 
    // use flutter to blank lower stick vals
    if (getLeftSpeed() < (FLUTTER+TK_MIN_THROTTLE)) 
        setLeftSpeed(TK_MIN_THROTTLE);
    if (getRightSpeed() < (FLUTTER+TK_MIN_THROTTLE)) 
        setRightSpeed(TK_MIN_THROTTLE);
    if (getLeftSpeed() > (TK_MAX_THROTTLE-FLUTTER)) 
        setLeftSpeed(TK_MAX_THROTTLE);
    if (getRightSpeed() > (TK_MAX_THROTTLE-FLUTTER)) 
        setRightSpeed(TK_MAX_THROTTLE);

    if (getLeftSpeed() <= 0) {
        getLeftEsc()->stop();
    } else if (!(curLeftSpeed >= (getLeftSpeed() - getSettings()->getFlutter()) && curLeftSpeed <= (getLeftSpeed() + getSettings()->getFlutter())))  {
        getLeftEsc()->speed(getLeftSpeed());
    }

    if (getRightSpeed() <= 0) {
        getRightEsc()->stop();
    } else if (!(curRightSpeed >= (getRightSpeed() - getSettings()->getFlutter()) && curRightSpeed <= (getRightSpeed() + getSettings()->getFlutter()))) {
        getRightEsc()->speed(getRightSpeed());
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
        setLeftSpeed(0);
        setRightSpeed(0);
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
    getLeftEsc()->arm();
    getRightEsc()->arm();
    delay(10); 
}

int Mixer::scaleSpeed(int rxVal) { 
    rxVal = constrain(rxVal, getSettings()->getRxrangeMin(), getSettings()->getRxrangeMax());
    return map(rxVal, getSettings()->getRxrangeMin(), getSettings()->getRxrangeMax(), TK_MIN_THROTTLE, TK_MAX_THROTTLE);
    //return 1000 * (((double)rxVal - _cfg->getRxrangeMin()) / (_cfg->getRxrangeMax() - _cfg->getRxrangeMin())) + 1000;
}

bool Mixer::isDeadzone(int speed)
{
    if (speed >= (TK_MID_THROTTLE - getSettings()->getDeadzone()) && speed <= (TK_MID_THROTTLE + getSettings()->getDeadzone()))
        return true;
    return false;
}

int Mixer::getLeftSpeed(void) {
    return _leftSpeed;
}
int Mixer::getRightSpeed(void) {
    return _rightSpeed;
}
Esc* Mixer::getLeftEsc(void) {
    return _leftEsc;
}
Esc* Mixer::getRightEsc(void) {
    return _rightEsc;
}
void Mixer::setLeftSpeed(int i) {
    i = constrain(i, TK_MIN_THROTTLE, TK_MAX_THROTTLE);
    _leftSpeed = i;
}
void Mixer::setRightSpeed(int i) {
    i = constrain(i, TK_MIN_THROTTLE, TK_MAX_THROTTLE);
    _rightSpeed = i;
}

Settings* Mixer::getSettings(void) {
    return _cfg;
}
PPMReader* Mixer::getPpm(void) {
    return _ppm;
}