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
 * This is a pin config for the Blackpill board
 * 
 * 
 */
#ifndef TK_STM32F111CE_H
#define TK_STM32F111CE_H


/*
 * LED Pin, used when the system is armed.
 */
#define LED_PIN                 PC13

/*
 * LED Pin, used when the system is armed.
 */
#define BTN_PIN                 PA0

/*
 * PPM Receiver pin
 */
#define PPM_RX_PIN             PB7

/*
 * ESC pins
 */
#define ESC0_PIN                PA5
#define ESC1_PIN                PA6

/*
 * Servo 0 Use this servo for the pan of a cam mount.
 */
#define SVO0_PIN                PA7

/*
 * Servo 1 Use this servo for the tilt of a cam mount.
 */
#define SVO1_PIN                PA8





#endif   /*  TK_STM32F111CE_H */
