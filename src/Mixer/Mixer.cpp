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

void Mixer::setup(void) { 
    
}

void Mixer::loop(void) {

    // TODO: This needs to be moved to the Betacrawer.cpp loop
    // TODO: Failsafe - Find a way to detect signal loss in a PPM signal
    //       Need to do further testing on other receivers see if we can get this 
    //       code to work as expected
    // 
    // if (getPpm()->timeSinceLastPulse() > SIGNAL_LOSS_TIME) {
    //     // shutdown (no paulses)
    //     //if(isArmed())
    //     //setArm(false);
    //         Serial.println("Signal Loss. (Failsafe)");
    //     return;
    // }

    // store prev values
    uint16_t px = getTx();
    uint16_t py = getTy();
    uint16_t pp = getPan();
    uint16_t pt = getTilt();

    // Get Throttle x,y values and pan and tilt values based on transmitter mode map
    if (getSettings()->hasReverse()) {  // if we have reverse use the centering Elevator stick for ESC throttle
        switch (getSettings()->getTxMode())
        {
            case 1: // {E,R}
            case 4: // {E,R}
                setTx(getChannel('R'));
                setTy(getChannel('E'));
                setPan(getChannel('A'));
                setTilt(getChannel('T'));
                break;
            case 2: // {E,A}
            case 3: // {E,A}
                setTx(getChannel('A'));
                setTy(getChannel('E'));
                setPan(getChannel('R'));
                setTilt(getChannel('T'));
                break;
        }
    } else {    // If no reverse use the non-centering throttle stick as ESC trhottle
        switch (getSettings()->getTxMode())
        {
            case 1: // {T,A}
            case 4: // {T,A}
                setTx(getChannel('A'));
                setTy(getChannel('T'));
                setPan(getChannel('R'));
                setTilt(getChannel('E'));
                break;
            case 2: // {T,R}
            case 3: // {T,R}
                setTx(getChannel('R'));
                setTy(getChannel('T'));
                setPan(getChannel('A'));
                setTilt(getChannel('E'));
                break;
        }
    }
    setAux(getChannel('1'), 1);
    setAux(getChannel('2'), 2);
    setAux(getChannel('3'), 3);
    setAux(getChannel('4'), 4);

    // Fix Flutter
    // tx = filterFlutter(px, tx);
    // ty = filterFlutter(py, ty);

    // Apply deadzone
    setTx(filterDeadzone(getTx()));
    setTy(filterDeadzone(getTy()));


    // This assigns the stick position to their respective left/right ESC values
    // calculate esc throttle values from the tx, ty values
    calculateThrottle(getTx(), getTy());

    // Limit throttle mins/max
    addExpo((getSettings()->getExpo()/10.0));

}

/**
 * 
 * @see https://forum.arduino.cc/t/from-linear-to-exponential-pwm-output/103036/10
 * @see http://randomhacksdrc.blogspot.com/2017/08/setting-up-throttle-curve-for-arduino.html 
 */
void Mixer::addExpo(float exp) {
    uint16_t tLeft = getLeft();
    uint16_t tRight = getRight();
    
    if (getSettings()->hasReverse()) {
        int16_t l = getLeft() - ESC_MID_THROTTLE;
        int16_t r = getRight() - ESC_MID_THROTTLE;
        int16_t rm = ESC_MAX_THROTTLE - ESC_MID_THROTTLE;        // relative max (500)
        
        // need to calculate forward expo then reverse expo
        //Serial.println("*  " + String(tLeft) + " " + String(tRight)  + "  *");
        //Serial.println("*  " + String(exp) + "  " + String(l) + " " + String(r)  + " " + String(rm) + "  *");
        
        if (getLeft() >= ESC_MID_THROTTLE) {  // forward
            l = (uint16_t)fscale(0, rm, 0, rm, l, exp);
            tLeft = ESC_MID_THROTTLE+l;
        } else if (getLeft() < ESC_MID_THROTTLE) {   // reverse
            l = abs(l);
            l = (uint16_t)fscale(0, rm, 0, rm, l, exp);
            tLeft = ESC_MID_THROTTLE-l;
        }
        if (getRight() >= ESC_MID_THROTTLE) {  // forward
            r = (uint16_t)fscale(0, rm, 0, rm, r, exp);
            tRight = ESC_MID_THROTTLE+r;
        } else if (getRight() < ESC_MID_THROTTLE) {   // reverse
            r = abs(r);
            r = (uint16_t)fscale(0, rm, 0, rm, r, exp);
            tRight = ESC_MID_THROTTLE-r;
        }
    } else {
        int16_t l = getLeft() - ESC_MIN_THROTTLE;
        int16_t r = getRight() - ESC_MIN_THROTTLE;
        uint16_t rm = ESC_MAX_THROTTLE - ESC_MIN_THROTTLE;        // relative max (1000)
        if (getLeft() > ESC_MIN_THROTTLE) {  // forward
            l = (uint16_t)fscale(0, rm, 0, rm, l, exp);
            tLeft = ESC_MID_THROTTLE+l;
        }
        if (getRight() > ESC_MIN_THROTTLE) {  // forward
            r = (uint16_t)fscale(0, rm, 0, rm, r, exp);
            tRight = ESC_MID_THROTTLE+r;
        }
    }
    setLeft(tLeft);
    setRight(tRight);
}

uint16_t Mixer::filterFlutter(uint16_t prev, uint16_t curr) {
    if ( 
        (curr >= (prev - getSettings()->getFlutter())) && 
        (curr <= (prev + getSettings()->getFlutter()))
        ) {
        return prev;
    }
    return curr;
}

uint16_t Mixer::filterDeadzone(uint16_t speed) {
    if (getSettings()->hasReverse()) {
        if (speed >= (ESC_MID_THROTTLE - getSettings()->getDeadzone()) && speed <= (ESC_MID_THROTTLE + getSettings()->getDeadzone()))
            return ESC_MID_THROTTLE;
    } else {
        if (speed < ESC_MIN_THROTTLE + getSettings()->getDeadzone()) {
            return ESC_MIN_THROTTLE;
        }
    }
    return speed;
}

/**
 * 
 */
void Mixer::calculateThrottle(uint16_t x, uint16_t y) {
    uint16_t leftSub = 0;
    uint16_t rightSub = 0;
    float pct = 0.0;
    uint16_t tRight = y;
    uint16_t tLeft = y;

    // Here we will find the percentage of left/right movement and subtract that from the current throttle
    // This should result in more subttle turns of the motors
    if (x > ESC_MID_THROTTLE) {                 // Left
        rightSub =  x-ESC_MID_THROTTLE;
        pct = (float)1.0-(rightSub/500.0);
        tRight = (uint16_t)((pct*(float)(y-ESC_MIN_THROTTLE))+ESC_MIN_THROTTLE);
    } else if (x < ESC_MID_THROTTLE) {          // Right
        leftSub = ESC_MID_THROTTLE-x;
        pct = (float)1.0-(leftSub/500.0);
        tLeft = (uint16_t)((pct*(float)(y-ESC_MIN_THROTTLE))+ESC_MIN_THROTTLE);
    }
    setLeft(tLeft);
    setRight(tRight);
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

uint16_t Mixer::getTx(void) {
    return tx;
}
uint16_t Mixer::getTy(void) {
    return ty;
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
void Mixer::setTx(uint16_t i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    tx = i;
}
void Mixer::setTy(uint16_t i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    ty = i;
}
void Mixer::setLeft(uint16_t i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    left = i;
}
void Mixer::setRight(uint16_t i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    right = i;
}
void Mixer::setPan(uint16_t i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    pan = i;
}
void Mixer::setTilt(uint16_t i) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    tilt = i;
}
void Mixer::setAux(uint16_t i, int chan) {
    i = constrain(i, ESC_MIN_THROTTLE, ESC_MAX_THROTTLE);
    switch (chan) {
        case 1:
            aux1 = i;
            break;
        case 2:
            aux2 = i;
            break;
        case 3:
            aux3 = i;
            break;
        case 4:
            aux4 = i;
            break;
    }
}

String Mixer::toString(void) {
    String str = "";
    str += "[" + String(tx) + " ";
    str += String(ty) + "] ";
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



/** 
 * fscale
 *   Floating Point Autoscale Function V0.1
 *   Paul Badger 2007
 *   Modified from code by Greg Shakar
 *  
 *   This function will scale one set of floating point numbers (range) to another set of floating point numbers (range)
 *   It has a "curve" parameter so that it can be made to favor either the end of the output. (Logarithmic mapping)
 *  
 *   It takes 6 parameters
 *  
 *   originalMin - the minimum value of the original range - this MUST be less than origninalMax
 *   originalMax - the maximum value of the original range - this MUST be greater than orginalMin
 *  
 *   newBegin - the end of the new range which maps to orginalMin - it can be smaller, or larger, than newEnd, to facilitate inverting the ranges
 *   newEnd - the end of the new range which maps to originalMax  - it can be larger, or smaller, than newBegin, to facilitate inverting the ranges
 *  
 *   inputValue - the variable for input that will mapped to the given ranges, this variable is constrained to originaMin <= inputValue <= originalMax
 *   curve - curve is the curve which can be made to favor either end of the output scale in the mapping. Parameters are from -10 to 10 with 0 being
 *            a linear mapping (which basically takes curve out of the equation)
 *  
 *   To understand the curve parameter do something like this:
 *  
 *   void loop(){
 *    for ( j=0; j < 200; j++){
 *      scaledResult = fscale( 0, 200, 0, 200, j, -1.5);
 *  
 *      Serial.print(j, DEC);  
 *      Serial.print("    ");  
 *      Serial.println(scaledResult, DEC);
 *    }  
 *  }
 *  
 *  And try some different values for the curve function - remember 0 is a neutral, linear mapping
 *  
 *  To understand the inverting ranges, do something like this:
 *  
 *   void loop(){
 *    for ( j=0; j < 200; j++){
 *      scaledResult = fscale( 0, 200, 200, 0, j, 0);
 *  
 *      //  Serial.print lines as above
 *  
 *    }  
 *  }
 * 
 * @see https://playground.arduino.cc/Main/Fscale/
 *  
 **/
float Mixer::fscale( float originalMin, float originalMax, float newBegin, float newEnd, float inputValue, float curve) {

  float OriginalRange = 0;
  float NewRange = 0;
  float zeroRefCurVal = 0;
  float normalizedCurVal = 0;
  float rangedValue = 0;
  bool invFlag = 0;

  // condition curve parameter
  // limit range
  if (curve > 10) curve = 10;
  if (curve < -10) curve = -10;

  curve = (curve * -.1) ; // - invert and scale - this seems more intuitive - postive numbers give more weight to high end on output
  curve = pow(10, curve); // convert linear scale into lograthimic exponent for other pow function

  /*
   Serial.println(curve * 100, DEC);   // multply by 100 to preserve resolution  
   Serial.println();
   */

  // Check for out of range inputValues
  if (inputValue < originalMin) {
    inputValue = originalMin;
  }
  if (inputValue > originalMax) {
    inputValue = originalMax;
  }

  // Zero Refference the values
  OriginalRange = originalMax - originalMin;

  if (newEnd > newBegin){
    NewRange = newEnd - newBegin;
  } else {
    NewRange = newBegin - newEnd;
    invFlag = 1;
  }

  zeroRefCurVal = inputValue - originalMin;
  normalizedCurVal  =  zeroRefCurVal / OriginalRange;   // normalize to 0 - 1 float

  /*
  Serial.print(OriginalRange, DEC);  
   Serial.print("   ");  
   Serial.print(NewRange, DEC);  
   Serial.print("   ");  
   Serial.println(zeroRefCurVal, DEC);  
   Serial.println();  
   */

  // Check for originalMin > originalMax  - the math for all other cases i.e. negative numbers seems to work out fine
  if (originalMin > originalMax ) {
    return 0;
  }

  if (invFlag == 0){
    rangedValue =  (pow(normalizedCurVal, curve) * NewRange) + newBegin;

  } else {    // invert the ranges  
    rangedValue =  newBegin - (pow(normalizedCurVal, curve) * NewRange);
  }

  return rangedValue;
}