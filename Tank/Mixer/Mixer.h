

#ifndef TK_MIXER_H
#define TK_MIXER_H

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

#include "configuration.h"
#include "Settings/Settings.h"
#include "PPMReader/PPMReader.h"
#include "Esc/Esc.h"


class Mixer {
  public:
     Mixer(Settings* pCfg, PPMReader* pPpm);
    ~Mixer();

    void setup(void);
    void loop(void);
    
    void arm(bool b);        // arm/disarm escs
    bool isArmed(void);
    bool isDeadzone(int speed);
    int getLeftSpeed(void);
    int getRightSpeed(void);
    Esc* getLeftEsc(void);
    Esc* getRightEsc(void);

    Settings* getSettings(void);
    PPMReader* getPpm(void);
    
  private:
    bool _armed = false;
    int _leftSpeed = 0;
    int _rightSpeed = 0;

    Settings* _cfg;
    PPMReader* _ppm;
    Esc* _leftEsc;
    Esc* _rightEsc;

    int scaleSpeed(int rxVal);
    void writeEscSpeed(void);
    void armSvo(void);
    void setLeftSpeed(int);
    void setRightSpeed(int);

};

#endif  /* TK_MIXER_H */