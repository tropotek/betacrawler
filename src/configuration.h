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
 * Edit this file to get the default values for your setup.
 * 
 * You may also want to look in the pins folder if you want to change the wiring pins.
 * 
 * If you are using an Arduino/AVR CPU then this is where you come to update 
 * your Betacrawler settings as the CLI will not work on single serial AVR CPUS.
 * 
 * If you are using STM32 setting can be changed later in the serial CLI
 * 
 */
#ifndef TK_CONFIGURATION_H
#define TK_CONFIGURATION_H

/*
 * Set to true if we want to enable Debug environment
 */
#define DEBUG                       true

/*
 * Serial baud rate
 */
#define SERIAL_BAUD                 115200

/*
 * define the max number of receiver channels 0=thr, 1=aile, 2=elev, 3=rudd, 4=aux1(arm), 5=aux2
 * (Valid values:4-16)
 * @todo: put this into the settings object
 */
#define MAX_RX_CHANNELS             7   // Available Channels

/*
 * The default transmitter channel map.
 * 
 * TEAR ARET etc.. refer to the channel order received by the receiver. 
 * So TAER means:
 *   Channel 1 -> thottle
 *   Channel 2 -> aileron
 *   Channel 3 -> elevator
 *   Channel 4 -> Rudder
 * @see https://oscarliang.com/channel-map/
 */
#define DEFAULT_TX_MAP              "TAER1234"

/*
 * The Default transmitter mode setup (Valid values 1-4)
 */
#define DEFAULT_TX_MODE             2

/*
 * Smooth out the stick control data
 * (Valid values 0-100)
 */
#define DEFAULT_FLUTTER             10

/*
 * Deadzone for center stick control
 * (Valid values 0-100)
 */
#define DEFAULT_DEADZONE            50

/*
 * Add throttle expo
 * (Valid values 1-100)
 */
#define DEFAULT_EXPO                -30

/*
 * Add throttle limit by percent
 * (Valid values 1-100)
 */
#define DEFAULT_THROTT_LIMIT        40

/*
 * The default ESC reverse mode
 * Can only be enabled if you you have bi-directional ECS's installed
 * This will also determin what stick is the throttle controller
 * 
 * Enabled (true): The Elevator stick will be used (centering)
 * Dissabled (false): The Throttle stick will be used (non-centering)
 * 
 */
#define DEFAULT_REVERSE             true


/*
 * Enable/Disable the firmware
 * 
 * NOTE: This will only work for STM32, the CLI does not work for arduino`s (AVR)
 */
#if !defined(BC_AVR)   // Not available for Arduino basic boards
#define BC_CLI                      true
#endif

// DO NOT MOVE This....
#include "Application.h"
#endif   /*  CONFIGURATION_H */
