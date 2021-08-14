/*
 * Copyright (C) 2021  www.tropotek.com
 * Project: Betacrawler
 * Facebook: https://www.facebook.com/groups/307432330496662/
 * Github: https://github.com/tropotek/betacrawler 
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * **********************************************************************
 * 
 */
#ifndef TK_APPLICATION_H
#define TK_APPLICATION_H

/*
 * Set to 1 if we want to enable Debug messages
 */
#ifndef DEBUG
#define DEBUG   0
#endif

/* 
 * Current build version
 */
#ifndef VERSION
#define VERSION                 "2.0.0"
#endif

/* 
 * Build Date
 */
#ifndef BUILD_DATE
#define BUILD_DATE              "07-07-2021"
#endif

/*
 * The common project name
 */
#ifndef PROJECT_NAME
#define PROJECT_NAME            "Beatacrawler"
#endif

/* 
 * Author
 */
#ifndef AUTHOR
#define AUTHOR                  "Tropotek"
#endif

/*
 * ESC Settings
 * NOTE: Assumes ESC are set to bi-directional.
 */
#ifndef ESC_MIN_THROTTLE
#define ESC_MIN_THROTTLE         1000       // Full reverse
#endif
#ifndef ESC_MID_THROTTLE
#define ESC_MID_THROTTLE         1500       // Stop
#endif
#ifndef ESC_MAX_THROTTLE
#define ESC_MAX_THROTTLE         2000       // Full Forward
#endif
#ifndef ESC_ARM
#define ESC_ARM                  500
#endif
#ifndef BC_MIN_ANGLE
#define BC_MIN_ANGLE            0
#endif
#ifndef BC_MAX_ANGLE
#define BC_MAX_ANGLE            180
#endif


#endif   /*  CONFIGURATION_H */
