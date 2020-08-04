

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
#include <Servo.h>


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
    int getPanAngle(void);
    Esc* getLeftEsc(void);
    Esc* getRightEsc(void);
    Servo* getPanServo(void);

    Settings* getSettings(void);
    PPMReader* getPpm(void);
    
  private:
    bool _armed = false;
    int _leftSpeed = 0;
    int _rightSpeed = 0;
    int _panAngle = 0;

    Settings* _cfg;
    PPMReader* _ppm;
    Esc* _leftEsc;
    Esc* _rightEsc;
    Servo* _panServo;

    int scaleSpeed(int rxVal);
    void writeEscSpeed(void);
    void writeServoSpeed(void);
    void armEsc(void);

    void setLeftSpeed(int i);
    void setRightSpeed(int i);
    void setPanAngle(int i);

};

#endif  /* TK_MIXER_H */