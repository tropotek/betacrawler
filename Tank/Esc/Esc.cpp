/*
 * Electronic Speed Controller (ESC) - Library
 *
 * https://github.com/RB-ENantel/RC_ESC
 */

#include "Esc.h"

/*
 * Sets the proper pin to output.
 */
// Esc::Esc(int pin, int outputMin, int outputMax, int armVal)
// {
// 	_pin = pin;
// 	_min = outputMin;
// 	_max = outputMax;
// 	_arm = armVal;

// 	_esc.attach(_pin); 
// }
Esc::Esc(int pin)
{
	_pin = pin;
	// _min = 1000;
	// _max = 2000;
	// _arm = 500;
	//_esc.attach(_pin); 
}

Esc::~Esc(){ }

/*
 * Calibrate the maximum and minimum PWM signal the ESC is expecting
 * depends on the outputMin, outputMax values from the constructor
 */
void Esc::calib(void)
{
	if (!_esc.attached()) _esc.attach(_pin);
	_esc.writeMicroseconds(_max);
	delay(_calibrationDelay);
	_esc.writeMicroseconds(_min);
	delay(_calibrationDelay);
	arm();
}

/*
 * Sent a signal to Arm the ESC
 * depends on the Arming value from the constructor
 */
void Esc::arm(void)
{
	if (!_esc.attached()) _esc.attach(_pin);
	_esc.writeMicroseconds(_arm);
}

/*
 * Sent a signal to stop the ESC
 * depends on the ESC stop pulse value
 */
void Esc::stop(void)
{
	if (!_esc.attached()) _esc.attach(_pin);
	_esc.writeMicroseconds(_stopPulse);
}

/*
 * Sent a signal to set the ESC speed
 * depends on the calibration minimum and maximum values
 */
void Esc::speed(int speed)
{
	if (!_esc.attached()) _esc.attach(_pin);
	_speed = constrain(speed, _min, _max);
	_esc.writeMicroseconds(_speed);
}

int Esc::getSpeed(void)
{
	if (!_esc.attached()) _esc.attach(_pin);
	return _esc.readMicroseconds();
	//return _speed;
}

/*
 * Set the current calibration delay in miliseconds
 *
 */
void Esc::setCalibrationDelay(int calibrationDelay)
{
	_calibrationDelay = calibrationDelay;
}

/*
 * Set the current Stop pulse in microseconds
 *
 */
void Esc::setStopPulse(int stopPulse)
{
	_stopPulse = stopPulse;
}
