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
#include "Application.h"

#define RX_MIN                  1140
#define RX_MAX                  1860
#define FLUTTER                 10
#define STICK_MODE              MODE_SINGLE
//#define STICK_MODE              MODE_DUAL
#define CAM_ENABLED             true        // Enable the cam servo 0


// Serial baud rate
#define SERIAL_BAUD             115200

// PPM Receiver pin
#define PPM_RX_PIN              3

// ESC pins
#define ESC0_PIN                8
#define ESC1_PIN                9

// Servo 0 Use this servo for pan of a cam mount.
#define SVO0_PIN                11

// LED Pin
#define LED_PIN                 13

// Debug: uncomment for more verbose debug 
//        messages and debug commands on the cli
#define DEBUG

// If you want the pan to be diabled on disarm
#define DISABLE_PAN_ON_DISARM

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

// PAN CONTROL channel (Could use CH_AUX2 mapped to a pot on controller too)
#if (CAM_ENABLED == MODE_SINGLE)
    #define CH_PAN              CH_3
#else
    #define CH_PAN              CH_2  
#endif

// TODO: These are not implemented yet
// ********************************************************************
// Use these to invert the stick readings from the PPM receiver
// NOTE: this does not change the motor direction
#define INVERT_ESC0             0
#define INVERT_ESC1             0
#define INVERT_SVO0             0

#define DEADZONE                0   // Set to 50 when using reverse
// ********************************************************************




#endif   /*  CONFIGURATION_H */
