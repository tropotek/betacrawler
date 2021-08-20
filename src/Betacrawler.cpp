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
 * 
 */
#include "Betacrawler.h"

Betacrawler::Betacrawler(Mixer* pMixer, Throttle* pThrottle, Cmd* pCli) {
    mixer = pMixer;
    throttle = pThrottle;
    cli = pCli;
}
Betacrawler::Betacrawler(Mixer* pMixer, Throttle* pThrottle) {
    mixer = pMixer;
    throttle = pThrottle;
}
Betacrawler::~Betacrawler() { }

void Betacrawler::setup(void) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    getSettings()->init();
    getThrottle()->enableReverse(getSettings()->hasReverse());
    getThrottle()->setup();

    getMixer()->setup();
    
    // We an object to manage the pan and tilt gimbol here
    // Update the pins file to your board to enable tilt (SVO1_PIN must be defined)
#if defined(SVO1_PIN) // Pan & Tilt
    cam = new Pan(SVO0_PIN, SVO1_PIN);
#elif defined(SVO0_PIN)   // Pan only
    cam = new Pan(SVO0_PIN);
#endif

    if (getCam() != nullptr) {
        getCam()->setup();
        getCam()->setFlutter(getSettings()->getFlutter()*2);
    }

}

void Betacrawler::loop(void) {
    // Do not edit these lines
    static uint16_t tick = 0;
    tick++;
    if (tick%200 == 0) { 
        bcLoop();
        return;
    }

    /*
     * ---------------- Custom code below. ----------------
     * 
     * Here you can write custom code to handle the the free stick
     * movements and all aux channels 2, 3, and 4. More if you have a receiver
     * with more than 8 channels
     * 
     * The current code uses the free stick as a pan/tilt control
     * for a fpv camera servo. But you can set it up for whatever you like
     * I used this pan platform CK-Tank: https://www.thingiverse.com/thing:4101341
     * Another option could be pan & tilt: https://www.thingiverse.com/thing:4211268
     * 
     * For the buttons, AUX1 is always the arm button and should not be used for anything else.
     * Below are some examples of using the other button channels.
     * 
     * Use getThrottle()->isArmed() to only perform tasks when arm switch enabled
     * 
     */


    // Send values to the Pan/Tilt camera servos
    if (tick%500 == 0 && getCam() != nullptr) {
        getCam()->setPan(getMixer()->getPan());
        getCam()->setTilt(getMixer()->getTilt());
        // Update Pan
        getCam()->loop();
    }

    // --------------------- EXAMPLE AUX CODE ---------------------
    // TODO: Write code here to perform functions to use Aux2, Aux3, Aux4
    // Two Position Switch (Values should be close to 1000 = POS1, 2000 = POS2)
    if (getMixer()->getAux(2) > 1500) { 
        if (getThrottle()->isArmed()) { // Use this to see if the motors are armed
            // Aux2 POS2 do some function if armed and Aux 2 is moved
        }
    }

    // Three Position Switch (Values should be close to 1000 = POS1, 1500 = POS2, 2000 = POS3)
    if (getMixer()->getAux(3) > 1000 && getMixer()->getAux(3) < 1200) {
        // AUX3 POS1
    } else if (getMixer()->getAux(3) > 1200 && getMixer()->getAux(3) < 1700) {
        // AUX3 POS2
    } else {
        // AUX3 POS3
    }

    // An idea could be to add LEDs and change their colors based on the tank status (Armed = red, etc)


    // Good place to output debug data to serial
    if (tick%500 == 0) { 
        // getSerial()->print("                                                                     \r");
        // getSerial()->print(getMixer()->toString());
        // getSerial()->print("\r");
   }
}


/**
 * You should not need to edit this main BC loop
 * I have removed it from the main loop() function 
 * to here to avoid issues.
 */
void Betacrawler::bcLoop(void) {
    if (getCli() != nullptr)
        getCli()->loop();

    // Read values from the PPM receiver and map channel values
    getMixer()->loop();

    // Read the Arm button and arm the tank if enabled
    getThrottle()->arm((getMixer()->getArm() > 1250));
    // Set the Esc throrrle values
    getThrottle()->setLeftSpeed(getMixer()->getLeft());
    getThrottle()->setRightSpeed(getMixer()->getRight());
    // transmit Esc throttle values
    getThrottle()->loop();
}

Mixer* Betacrawler::getMixer(void) {
    return mixer;
}
Cmd* Betacrawler::getCli(void) {
    return cli;
}
Settings* Betacrawler::getSettings(void) {
    return getMixer()->getSettings();
}
Stream* Betacrawler::getSerial(void) {
    return &Serial; // TODO: not sure if this is a good Idea, maybe we should pass it through from the constructor???
}
PPMReader* Betacrawler::getPPM(void) {
    return getMixer()->getPpm();
}
Throttle* Betacrawler::getThrottle(void) {
    return throttle;
}
Pan* Betacrawler::getCam(void) {
    return cam;
}
