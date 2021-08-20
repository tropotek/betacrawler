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
 * This is a pin config for the Arduino boards
 * 
 */
#ifndef BC_AVR_H
#define BC_AVR_H


/*
 * LED Pin, used when the system is armed.
 */
#define LED_PIN                 13

/* 
 * Button 
 * TODO: Unused yet
 */
//#define BTN_PIN                 0

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
//#define SVO1_PIN                11





#endif   /*  BC_AVR_H */
