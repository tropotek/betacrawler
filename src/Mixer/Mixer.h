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

#ifndef BC_MIXER_H
#define BC_MIXER_H


#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif
#include "configuration.h"
#include "Settings/Settings.h"
#include "PPMReader/PPMReader.h"
#include "Throttle/Throttle.h"

// #include "Esc/Esc.h"
// #include <Servo.h>

#ifndef SIGNAL_LOSS_TIME
/*
 * After this timeout the PPM signal will failsafe
 */
#define SIGNAL_LOSS_TIME        25000
#endif

class Mixer {
  public:
     Mixer(PPMReader* pPpm, Settings* pSettings);
    ~Mixer();

    void setup(void);
    void loop(void);
    
    Settings* getSettings(void);
    PPMReader* getPpm(void);

    uint16_t getChannel(char c);
    uint16_t getTx(void);
    uint16_t getTy(void);
    uint16_t getLeft(void);
    uint16_t getRight(void);
    uint16_t getPan(void);
    uint16_t getTilt(void);
    uint16_t getArm();
    uint16_t getAux(int i);
    
    String toString(void);
    
  private:
    Settings* settings;
    PPMReader* ppm;

    uint16_t tx, ty;        // Throttle control points
    uint16_t left, right;   // Calculated Esc throttle vals
    uint16_t pan, tilt;     // cam servo controls
    uint16_t aux1;          // Aux 1 used as Arm 
    uint16_t aux2;          // Aux 2
    uint16_t aux3;          // Aux 3
    uint16_t aux4;          // Aux 4

    uint16_t T_MIN;          // 
    uint16_t T_MID;          // 
    uint16_t T_MAX;          // 

    void setTx(uint16_t i);
    void setTy(uint16_t i);
    void setLeft(uint16_t i);
    void setRight(uint16_t i);
    void setPan(uint16_t i);
    void setTilt(uint16_t i);
    void setAux(uint16_t i, int chan);

    uint16_t filterFlutter(uint16_t prev, uint16_t curr);
    uint16_t filterDeadzone(uint16_t ySpeed);

    void calculateThrottle(uint16_t x, uint16_t y);
    void addExpo(float exp);
    float fscale(float originalMin, float originalMax, float newBegin, float newEnd, float inputValue, float curve);

};

#endif  /* BC_MIXER_H */