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
 *
 * Resources
 *   PPM Reciver: https://github.com/Nikkilae/PPM-reader
 *   ESC/Servo: https://www.instructables.com/id/ESC-Programming-on-Arduino-Hobbyking-ESC/
 *   ESC bi-directional:  https://www.youtube.com/watch?v=jBr-ZLMt4W4
 *   ESC: https://github.com/MikeysLab/BrushlessESCviaPWM/blob/master/EscPWMTesting/EscPWMTesting.ino
 *   ESC: https://www.robotshop.com/community/blog/show/rc-speed-controller-esc-arduino-library
 *
 *
 * ESC's used in this project: HGLRC BS30A - BLHli_S: Rev16.6 F-H-40
 */

#ifndef TK_CONFIGURATION_H
#define TK_CONFIGURATION_H

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

#include <EEPROM.h>


// The common project name
const String PROJECT_NAME =     "Beatacrawler";

// Current build version
#define VERSION                 1

// Debug: uncomment for more verbose debug 
//        messages and debug commands on the cli
#define DEBUG

// Serial baud rate
#define SERIAL_BAUD             115200

// PPM Receiver pin
#define PPM_RX_PIN              3

// ESC pins
#define ESC0_PIN                8
#define ESC1_PIN                9

// Use these to invert the stick readings from the PPM receiver
// NOTE: this does not change the motor direction
#define INVERT_ESC0             0
#define INVERT_ESC1             0


// define the max number of receiver channels 0=thr, 1=aile, 2=elev, 3=rudd, 4=aux1(arm), 5=aux2
#define MAX_RX_CHANNELS         6   // Available Channels

// numer of used ESC/servo channels
#define ESC_MAX_CHANNELS        2

// Map channels to use with PPMReader
#define CH_1                    1
#define CH_2                    2
#define CH_3                    3
#define CH_4                    4
#define CH_AUX1                 5
#define CH_AUX2                 6


/*
 * ------------------------------------------------------------------
 *   DO NOT EDIT BELOW UNLESS YOU KNOW WHAT YOU ARE DOING !!!!!!!!!
 * ------------------------------------------------------------------
 */

/*
 * ARM SWITCH CHANNEL
 */
#define CH_ARM                  CH_AUX1

/*
 * SINGLE STICK CONTROL:
 *   This is the mapping of the stick modes only change this if you are using
 *   different mapping in your controller, or want to change the stick inputs
 */
#define CH_THROT                CH_1  // THROTTLE
#define CH_DIR                  CH_4  // RUDDER


/*
 * DUEL THROTTLE CONTROL:
 *   This mapping is for using independent sticks to control
 *   the left and right tracks individually
 */
#define CH_THROT_LEFT           CH_1  // THROTTLE
#define CH_THROT_RIGHT          CH_3  // ELEVATOR


// Stick modes
#define MODE_DUAL               0
#define MODE_SINGLE             1

#define TK_MIN_THROTTLE         1000
#define TK_MAX_THROTTLE         2000
#define TK_MID_THROTTLE         1500


// Uncomment to use the Servo lib only and not the Esc lib
//#define SERVO_LIB

#endif   /*  CONFIGURATION_H */
