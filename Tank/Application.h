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
 */

#ifndef TK_APPLICATION_H
#define TK_APPLICATION_H

#if defined(ARDUINO) && ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

/* ***********************************************
 * Application Specific
 * ***********************************************/

// The common project name
const String PROJECT_NAME =     "Beatacrawler";

// Current build version
#define VERSION                 1

#define TK_MIN_THROTTLE         1000
#define TK_MAX_THROTTLE         2000
#define TK_MID_THROTTLE         1500

#define TK_MIN_ANGLE            0
#define TK_MAX_ANGLE            180

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

// Stick modes
#define MODE_DUAL               0
#define MODE_SINGLE             1


// To setup the command line interface 
//   when we figure out how to handle the interrupts
//#define CLI_ENABLED





#endif   /*  TK_APPLICATION_H */
