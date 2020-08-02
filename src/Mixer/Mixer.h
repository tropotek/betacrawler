

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

#include <Servo.h>
#if !defined(SERVO_LIB)
#include "Esc/Esc.h"
#endif


class Mixer {
  public:
     Mixer(Settings* pCfg, PPMReader* pPpm);
    ~Mixer();

    void setup(void);
    void loop(void);
    
    void arm(bool b);        // arm/disarm escs
    bool isArmed(void);
    int scaleSpeed(int rxVal);
    bool isDeadzone(int speed);
    void writeEscSpeed(void);

    void armSvo(void);
    
  private:
    bool _armed = false;
    int _leftSpeed = 0;
    int _rightSpeed = 0;

    Settings* _cfg;
    PPMReader* _ppm;
#if !defined(SERVO_LIB)
    Esc* _leftEsc;
    Esc* _rightEsc;
#else
    Servo* _leftEsc;
    Servo* _rightEsc;
#endif

};

#endif  /* TK_MIXER_H */