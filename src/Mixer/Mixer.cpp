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
 */
#include "Mixer.h"


Mixer::Mixer(PPMReader* pPpm, Settings* pSettings) {
    ppm = pPpm;
    settings = pSettings;
}
Mixer::~Mixer() { }

void Mixer::setup(void) {  }

void Mixer::loop(void) {

    // TODO: Failsafe - Find a way to detect signal loss in a PPM signal
    //       Need to do further testing on other receivers see if we can get this 
    //       code to work as expected
    // 
    // if (getPpm()->timeSinceLastPulse() > SIGNAL_LOSS_TIME) {
    //     // shutdown (no paulses)
    //     //if(isArmed())
    //         Serial.println("Signal Loss. (Failsafe)");
    //     //arm(false);
    //     return;
    // }

    // Get Throttle x,y values and pan and tilt values based on transmitter mode map
    if (getSettings()->hasReverse()) {  // if we have reverse use the centering Elevator stick for ESC throttle
        switch (getSettings()->getTxMode())
        {
            case 1: // {E,R}
            case 4: // {E,R}
                tx = getChannel('R');
                ty = getChannel('E');
                pan = getChannel('A');
                tilt = getChannel('T');
                break;
            case 2: // {E,A}
            case 3: // {E,A}
                tx = getChannel('A');
                ty = getChannel('E');
                pan = getChannel('R');
                tilt = getChannel('T');
                break;
        }
    } else {    // If no reverse use the non-centering throttle stick as ESC trhottle
        switch (getSettings()->getTxMode())
        {
            case 1: // {T,A}
            case 4: // {T,A}
                tx = getChannel('A');
                ty = getChannel('T');
                pan = getChannel('R');
                tilt = getChannel('E');
                break;
            case 2: // {T,R}
            case 3: // {T,R}
                tx = getChannel('R');
                ty = getChannel('T');
                pan = getChannel('A');
                tilt = getChannel('E');
                break;
        }
    }
    aux1 = getChannel('1');
    aux2 = getChannel('2');
    aux3 = getChannel('3');
    aux4 = getChannel('4');

    // TODO: calculate esc throttle values from the tx, ty values
    calculateThrottle(tx, ty);
}

/**
 * 
 */
void Mixer::calculateThrottle(uint16_t x, uint16_t y) {
    int leftSub = 0;
    int rightSub = 0;
    float pct = 0.0;
    right = left = y;
    // Here we will find the percentage of left/right movement and subtract that from the current throttle
    // This should result in more subttle turns of the motors
    // TODO: implement a scaling factor like rate curve for turning harder or softer.
    if (x > ESC_MID_THROTTLE) {                 // Left
        rightSub =  x-ESC_MID_THROTTLE;
        pct = (float)1.0-(rightSub/500.0);
        right = (uint16_t)((pct*(float)(y-ESC_MIN_THROTTLE))+ESC_MIN_THROTTLE);
    } else if (x < ESC_MID_THROTTLE) {          // Right
        leftSub = ESC_MID_THROTTLE-x;
        pct = (float)1.0-(leftSub/500.0);
        left = (uint16_t)((pct*(float)(y-ESC_MIN_THROTTLE))+ESC_MIN_THROTTLE);
    }
}

/**
 * Return the channel value using the charaters T,A,E,R,1,2,3,4
 */
uint16_t Mixer::getChannel(char c) {
    char* map = getSettings()->getTxMap();
    for(int i = 0; i < strlen(map); i++) {
        if (c == map[i] && map[i] != '\0') {
            return getPpm()->latestValidChannelValue(i+1, ESC_ARM);
        }
    }
    return 0;
}

Settings* Mixer::getSettings(void) {
    return settings;
}
PPMReader* Mixer::getPpm(void) {
    return ppm;
}

uint16_t Mixer::getLeft(void) {
    return left;
}
uint16_t Mixer::getRight(void) {
    return right;
}
uint16_t Mixer::getPan(void) {
    return pan;
}
uint16_t Mixer::getTilt(void) {
    return tilt;
}
uint16_t Mixer::getArm() {
    return aux1;
}
uint16_t Mixer::getAux(int i) {
    i = constrain(i, 1, 4);
    switch (i) {
        case 1:
            return aux1;
        case 2:
            return aux2;
        case 3:
            return aux3;
        case 4:
            return aux4;
    }
    return aux1;
}

String Mixer::toString(void) {
    String str = "";
    str += String(getLeft()) + " ";
    str += String(getRight()) + " ";
    str += String(getPan()) + " ";
    str += String(getTilt()) + " ";
    str += String(getAux(1)) + " ";
    str += String(getAux(2)) + " ";
    str += String(getAux(3)) + " ";
    str += String(getAux(4)) + " ";
    return str;
}
