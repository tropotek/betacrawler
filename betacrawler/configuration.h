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



/*
 * Serial baud rate
 */
#define SERIAL_BAUD             115200

/*
 * Your controller's minumim stick value
 */
#define RX_MIN                  1140

/*
 * Your controller's maxumim stick value
 */
#define RX_MAX                  1860

/*
 * A filter to reduce stick position noise
 * Increase this if you find erratic motor movements 
 *   when the stick is in a hold position.
 * 
 */
#define FLUTTER                 10

/*
 * Set the mode of operation.
 * MODE_SINGLE: Use the left stick for all motor throttle movement
 * MODE_DUAL: Use left and right as seperate throttles for each motor
 */
#define STICK_MODE              MODE_SINGLE

/*
 * If you are using a servo to move your camera enable this
 */
#define CAM_PAN_ENABLED         true        // Enable cam servo 0
// TODO: Implement this. (Comming soon)
#define CAM_TILT_ENABLED        true        // Enable cam servo 1

/*
 * If you want the pan/tilt to be diabled on disarm
 */
#define DISABLE_PAN_TILT_ON_DISARM

// ------------- PIN Configuration -------------

/*
 * PPM Receiver pin
 */
#define PPM_RX_PIN              3

/*
 * ESC pins
 */
#define ESC0_PIN                8
#define ESC1_PIN                9

/*
 * Servo 0 Use this servo for the pan of a cam mount.
 */
#define SVO0_PIN                10

/*
 * Servo 1 Use this servo for the tilt of a cam mount.
 */
#define SVO1_PIN                11

/*
 * LED Pin, used when the system is armed.
 */
#define LED_PIN                 13

// ----------------------------------------------



/*
 * ------------------------------------------------------------------
 *   DO NOT EDIT BELOW UNLESS YOU KNOW WHAT YOU ARE DOING !!!!!!!!!
 * ------------------------------------------------------------------
 */

/*
 * Debug: uncomment for more verbose debug messages and debug commands on the cli
 */
#define DEBUG

/*
 * ARM SWITCH CHANNEL
 */
#define CH_ARM                  CH_AUX1

/*
 * SINGLE STICK CONTROL:
 *   This is the mapping of the stick modes only change this if you are using
 *   different mapping in your controller, or want to change the stick inputs
 */
// Transmitter MODE 2 (Left Stick throttle)
#define CH_THROT                CH_1  // Left stick up/down
#define CH_DIR                  CH_4  // Left stick left/right

// Transmitter MODE 1 (Right Stick throttle)
// #define CH_THROT                CH_3  // Right stick up/down
// #define CH_DIR                  CH_2  // Right stick left/right

/*
 * DUEL THROTTLE CONTROL:
 *   This mapping is for using independent sticks to control
 *   the left and right tracks individually
 */
#define CH_THROT_LEFT           CH_1  // Left stick up/down
#define CH_THROT_RIGHT          CH_3  // Right stick up/down

/*
 * PAN CONTROL channel (Could use CH_AUX2 mapped to a pot on controller too)
 */
#define CH_PAN                  CH_2  // Left stick left/right
#if (CAM_TILT_ENABLED == MODE_SINGLE)
    #define CH_TILT             CH_3  // Right stick up/down
#else
    #define CH_TILT             CH_4  // Left stick left/right
#endif

/*
 * TODO: These are not implemented yet
 * 
 * Use these to invert the stick readings from the PPM receiver
 * 
 * NOTE: This does not change the motor directiononly the stick reading direction, 
 *       you need to do that manually through blheli32 or wiring
 */
#define INVERT_ESC0             0
#define INVERT_ESC1             0
#define INVERT_SVO0             0
#define INVERT_SVO1             0

#define DEADZONE                0   // Set to 50 when reverse motors is enabled (not implemented)





#endif   /*  CONFIGURATION_H */
