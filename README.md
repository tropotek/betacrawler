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



