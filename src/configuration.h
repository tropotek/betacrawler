/*
 * Copyright (C) 2021  www.tropotek.com
 * Project: Betacrawler
 * Facebook: https://www.facebook.com/groups/307432330496662/
 * Github: https://github.com/tropotek/betacrawler 
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * ***********************************************************************
 * 
 * Resources
 *   PPM Reciver: https://github.com/Nikkilae/PPM-reader
 *   ESC/Servo: https://www.instructables.com/id/ESC-Programming-on-Arduino-Hobbyking-ESC/
 *   ESC bi-directional:  https://www.youtube.com/watch?v=jBr-ZLMt4W4
 *   ESC: https://github.com/MikeysLab/BrushlessESCviaPWM/blob/master/EscPWMTesting/EscPWMTesting.ino
 *   ESC: https://www.robotshop.com/community/blog/show/rc-speed-controller-esc-arduino-library
 *
 * ESC's used in this project: HGLRC BS30A - BLHli_S: Rev16.6 F-H-40
 * 
 */
#ifndef TK_CONFIGURATION_H
#define TK_CONFIGURATION_H


/*
 * Set to 1 if we want to enable Debug messages
 */
#define DEBUG   1

/* 
 * Current build version
 */
#define VERSION                 "2.0.0"

/* 
 * Build Date
 */
#define BUILD_DATE              "07-07-2021"

/*
 * The common project name
 */
#define PROJECT_NAME            "Beatacrawler"

/* 
 * Author
 */
#define AUTHOR                  "Tropotek"

/*
 * Serial baud rate
 */
#define SERIAL_BAUD             115200

/*
 * define the max number of receiver channels 0=thr, 1=aile, 2=elev, 3=rudd, 4=aux1(arm), 5=aux2
 */
#define MAX_RX_CHANNELS         7   // Available Channels


/*
 * ESC Settings
 * NOTE: Assumes ESC are set to bi-directional.
 */
#define ESC_MIN_THROTTLE         1000       // Full reverse
#define ESC_MID_THROTTLE         1500       // Stop
#define ESC_MAX_THROTTLE         2000       // Full Forward
#define ESC_ARM                  500





/*
 * Smooth out the stick control data
 */
#define FLUTTER          10

/*
 * The default transmitter channel map
 */
#define TX_MAP           "TAER"

/*
 * THe Default transmitter mode setup
 */
#define RX_MODE           2

/*
 * The default ESC reverse mode
 * Can only be enabled if you you have bi-directional ECS's installed
 * This will also determin what stick is the throttle controller
 * 
 * Enabled (true): The Elevator and Aileron (Mode 2) will be used
 * Dissabled (false): The Throttle and Rudder (Mode 2) will be used 
 * 
 * 
 */
#define REVERSE           false






#endif   /*  CONFIGURATION_H */
