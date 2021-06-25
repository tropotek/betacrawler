/*
 * Electronic Speed Controller (ESC) - Library
 *
 * https://github.com/RB-ENantel/RC_ESC
 */

#ifndef TK_ESC_H
#define TK_ESC_H

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

#include <Servo.h>

class Esc
{
	public:
		//Esc(int pin, int outputMin = 1000, int outputMax = 2000, int armVal = 500);
		Esc(int pin);
		~Esc();
		void calib(void);
		void arm(void);
		void stop(void);
		void speed(int speed);
		void setCalibrationDelay(int calibrationDelay);
		void setStopPulse(int stopPulse);
		int  getSpeed(void);

	private:
		Servo _esc;							// create servo object to control an Esc

		// Hardware
		int _pin;							// ESC output Pin

		// Calibration
		int _min = 1000; 
		int _max = 2000;
		int _speed = 1000;
		int _arm = 500;
		int _calibrationDelay = 8000;		// Calibration delay (milisecond)
		//int _stopPulse = 500;
		int _stopPulse = 1000;				// Stop pulse (microseconds)
};

#endif /*  TK_ESC_H  */
