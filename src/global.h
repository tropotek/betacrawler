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
 * Define any global objects here
 * 
 */

#ifndef BC_GLOBAL_H
#define BC_GLOBAL_H

#include "headers.h"


/* ***********************************************
 * Global variables
 * ***********************************************/

/*
 * Store the Betacrawler user defind settings
 */
// Settings cfg;

/*
 * The Command Line Interface to the USB Serial
 */
#ifdef BC_CMD
Cmd cmd(&Serial);
#endif

/*
 * Object to read the PPM signal from the receiver
 */
PPMReader ppm(PPM_RX_PIN, MAX_RX_CHANNELS);

/*
 * Mix down the received raw RX throttle signals to 
 * values ready to be sent to the ESC's
 */
// Mixer mixer(&cfg, &ppm);
Throttle throttle(ESC0_PIN, ESC1_PIN);



#endif   /*  BC_GLOBAL_H */
