Betacrawler
====


Introduction
====

Betacrawler is a basic tank control firmware for arduino processers.

It has some basic settings such as rxmin and rxmax and some minor stick filtering.   
These settings can be changed in the configuration.h file.

This project has been inspired somewhat by the Betaflight firmware

It has 2 modes of operation:
  - __Dual Stick:__ Use both sticks to control the left and right motors individually
  - __Single Stick:__ Use a single stick to control the left right
            motors, leaving the other stick availalbe for cam pan/tilt


Configuration
====

To configure the firmware for your setup open the configuration.h file and edit
the required parameters as needed.

```cpp
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
```

Also be sure to check the pins section to ensure you have your hardware connected correctly. 
You can change the default pins as requred.

```cpp
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
 * Servo 0 Use this servo for pan of a cam mount.
 */
#define SVO0_PIN                11

/*
 * LED Pin, used when the system is armed.
 */
#define LED_PIN                 13
```



Hardware Setup
====




Parts
=====
  - ESC's used in this project: HGLRC BS30A - BLHli_S: Rev16.6 F-H-40
  - Any brusheless motors that fit the ESC's
  - A battery 
  - A PPM Receiver with a transmitting controller to match





How To Contribute
==========

If you would like to contribute contact me and let me know on github or facebook at 

My knowledge of C++ and arduino is limited and I would like to see where this project could go in the future.

If we get enough of a community we can grow the features of Betacrawler.

If you arent a programmer and still want to contribute pleae considder supporting me by sending a donation through [PayPal] (https://www.paypal.com/paypalme/tropotek).
This will help me continue to support this project

Follow this project on [Facebook Group] (https://www.facebook.com/groups/307432330496662)


Resources Used
====
  - [PPM Reciver] (https://github.com/Nikkilae/PPM-reader)
  - [ESC/Servo] (https://www.instructables.com/id/ESC-Programming-on-Arduino-Hobbyking-ESC/)
  - [ESC bi-directional] (https://www.youtube.com/watch?v=jBr-ZLMt4W4)
  - [ESC PWM Testing] (https://github.com/MikeysLab/BrushlessESCviaPWM/blob/master/EscPWMTesting/EscPWMTesting.ino)
  - [Arduino ESC Lib] (https://www.robotshop.com/community/blog/show/rc-speed-controller-esc-arduino-library)



