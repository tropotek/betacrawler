

#ifndef TK_MIXER_H
#define TK_MIXER_H

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

#include "configuration.h"
#include "Settings.h"
#include "PPMReader.h"
#include "Esc.h"
#include <Servo.h>


class Mixer {
  public:
     Mixer(Settings* pCfg, PPMReader* pPpm);
    ~Mixer();

    void setup(void);
    void loop(void);
    
    void arm(bool b);
    bool isArmed(void);
    bool isDeadzone(int speed);
    int getLeftSpeed(void);
    int getRightSpeed(void);
    int getPanAngle(void);
    int getTiltAngle(void);
    Esc* getLeftEsc(void);
    Esc* getRightEsc(void);
    Servo* getPanServo(void);
    Servo* getTiltServo(void);

    Settings* getSettings(void);
    PPMReader* getPpm(void);
    
  private:
    bool _armed = false;
    int _leftSpeed = 0;
    int _rightSpeed = 0;
    int _panSpeed = 0;
    int _panAngle = 0;
    int _tiltSpeed = 0;
    int _tiltAngle = 0;

    Settings* _cfg;
    PPMReader* _ppm;
    Esc* _leftEsc;
    Esc* _rightEsc;
    Servo* _panServo;
    Servo* _tiltServo;

    int scaleSpeed(int rxVal);
    void writeEscSpeed(void);
    void writeServoSpeed(void);
    void armEsc(void);

    void setLeftSpeed(int i);
    void setRightSpeed(int i);
    void setPanAngle(int i);
    void setTiltAngle(int i);

};

#endif  /* TK_MIXER_H */