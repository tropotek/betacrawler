# Betacrawler


Facebook: [Betacrawler Community](https://www.facebook.com/groups/307432330496662)

Betacrawler is a RC tank or robot controler firmware for arduino and STM32 processors using a PPM controller 2 ESCs and a standard Radio Transmitter (OpenTx Recommended).  

Use [Atom](https://atom.io/) or [VSCode](https://code.visualstudio.com/download) with the [Platform IO](https://platformio.org/) plugin to compile.  
If your not sure how to use PlatformIO here is a tutorial to get you started (https://youtu.be/EIkGTwLOD7o)

If you have a model that requires tracks or any other 2 wheeled model that you wish to control with a single 
stick from a controler then this could be the base code for you.

The aim is to create a platform that uses a standard radio transmitter generaly using (OpenTx)[https://www.open-tx.org/downloads]. I have use a FrSky x-lite with the all-in-one module I had laying around for testing. Be sure that your radio and receiver are able to bind before using.

It controls 2 ESC's for throttle on one stick and the remaining stick can be configured to use a Pan/Tilt camera platform.
Then Aux 1 channel is reserved for Arming and you can configure any remaining channels and add your own code.

Betacrawler has a USB CLI (Command Line Interface) enabled for STM32 boards as I have not found a way to get Arduino boards
to read the serial while at the same time read in the data from the PPM radio receiver that uses an intterupt channel.
If anyone knows how to do this feel free to update the code and request that it be added to the main.

---
## Known Issues
 - Be carful when using Serial.println() or outputting to the serial. You may notice random signals getting sent to the ESC and Servos. I am still trying to find out the cause of this, could be something to do with the clock timing when sending the signals to the Servo.
 - I still cannot figure out how to use an interuppt for the PPM in conjuntion with the Serial CLI input for Arduino as there is a conflict there and the CLI commands get corrupted.

---

## Configuration

Before you compile for your board we need to do some configuration. This part assumes you have decided on your hardware and found the corret pin locations on your selected board. 

### platformio.ini

First you need to select the board variant you are going to use. 
```ini
[platformio]
;default_envs  = pro16MHzatmega328
default_envs  = blackpill_f411ce
;default_envs  = blackpill_f401cc
;default_envs  = genericSTM32F411CE
;default_envs  = genericSTM32F401CE

```

If you want to add your own or customise the buid you can edit the files found in the /ini folder. In theory any hardware that supports the Arduino framework can be used.


### configuration.h

Contains all the default values that will be used, if you are using the CLI these can be updated later, if you are using arduino then you will need to update these before you upload to the board.

### pins.h

In the /pins folder you have to select the board varian you are using and update the pin values to the locations you are using on your setup. If you have no SVO0_PIN or SVO1_PIN pins defined then the pan/tilt feature is disabled.

### Build Betacrawler

Now you should be able to compile the code. However if you get a Servo conflict or error. For STM32 boards, you may have to delete the folder in `.pio/libdeps/<boardNmae>/Servo` to force it to use the internal STM32 lib. 

---

## CLI 

### First run with CLI

First up you will list the config using `list` and you will see garble in the setting values. From here you need to import fresh settings to the Eeprom. So just type `reset` may take a min or two then type `save` and if you list the settings now you will see the default config.

### Available Commands:

```
> help
Use the following commands:
  help:  To view this help.
  show:  To show the current settings.
  get <param>: Display the value of a settings parameter.
  set <param> <value>: Set the value of a settings parameter.
  reset:  Factory Reset and save the settings.
  save:  Save settings to memory.
  reboot:  restart the system
```

Type `help` or `?` at anytime to see the main menu. Most of the options are self explanitory. Its the get and set options that we will explain further.

When you type `list` or `l` you will be shown the current setup.

```
> l
Settings:
 version        2.0.0
 flutter        10
 deadzone       50
 expo           -30
 tlimit         40
 txmode         2
 reverse        Enabled
 txmap          TAER1234
```

From here you can `get` or `set` any of these values (except version of course). 

### flutter
This value smoths out the stick movement and tries to compensate for any jitter in the sticks. A higher value the more you have to move the stick before it registers and a new position.

Valid Values: 0-100

### deadzone
The size of the deadzone at the stop point on the throttle stick. With reverse enabled this is around the center of the tick. With reverse disabled this deadzone affects the bottom position of the stick.

Valid Values: 0-100

### expo
Negative values require more stick movement to get to full throttle. Positive values require lest stick movement to register throttle change.

Valid values: -100 to 100

### tlimit
The percentage of the total throttle to allow. This allows you limit the trottle as needed

Valid Values: 1-100

### txmode
Based on your radio transmitter you can setup the radio mode type to allow Betacrawler to know how to use your radio.
If you are unsure what your radio mode is [try here](https://www.google.com/search?client=firefox-b-d&q=radio+transmitter+mode)

### reverse
If you have bi-direction enabled on your ESC's you will enable reverse. Then the throttle stick will be the self centering stick on the controller.

If you do not have bi-directional ESC's then you must set this ti false. and the throttle stick will be the one that does not self center.

You need to connect your ESC to Blheli or Configurator to change your ESC's.

[See here](https://oscarliang.com/flash-blheli-s-esc-firmware-fc-pass-through/) for more info.

### txmap
Depending on your radio type you can map the channles here.
You will need to chack your radio setup to configure this. This is generally a good default for OpenTX. 

[See Here](https://oscarliang.com/channel-map/) for more info.

---

## Hardware Setup

This setup assumes that you already have a radio transmitter and know how to bind it up to your selected PPM receiver.

It is only a suggested way using Arduino. If you need more info on this then feel free to join the Facebook group and ask any questions you may have.


#### Your Model

To start with you will need a Tank or robot model. If you have a 3D Printer and a bit of time on your hands
a fantastic starting model is the [RC Tank - By Staind](https://www.thingiverse.com/thing:2414983).

But I assume if you have come here then you have the model part sorted out already. ;-)


#### Step 1 - Arduino & PPM 

##### Parts
  * [Arduino Nano V3](https://www.banggood.com/custlink/mKDyWl3pU3)
  * [Arduino Nano Expansion Board](https://www.banggood.com/custlink/KKmRpavpqC) (optional: For bench testing)
  * [Tiny 2.4G DSM2 6CH PPM Receiver](https://www.banggood.com/custlink/GDmycOmcUI)

##### Wiring

![Arduino And PPM](media/ArduinoPPM.jpg)


#### Step 2 - ESC's

Note: If your ESC's have an onboard 5v BEC you can use that instad of using an external BEC on the PDB or similar.
      However it is reccomended to use the BEC's on you PDB where possible to reduce EMF noise.

##### Parts
  * [Matek Mini PDB With 5V/12V BEC](https://www.banggood.com/custlink/m3vErjGJM1)
  * [5V BEC 2-6S](https://www.banggood.com/custlink/mGDYJO3rfE) (Optional: not needed if PDB/ESC has 5v BEC)
  * [2 x Razor32 35A 3-6S ESC](https://www.banggood.com/custlink/v3GhrL3Jwc)
  * [2 x 2212 930KV 2-4s](https://www.banggood.com/custlink/mK3ypomrfZ)
  * [2200mAh 60C 3S Lipo](https://www.banggood.com/custlink/3GKyWLGtm5)

##### Wiring

![Arduino And PPM](media/ESC_Motor.jpg)



#### Step 3 - FPV & Servos

The main thing to take note here is that you have connected the correct voltage to your VTX.
If it needs 5v you must connect to a 5v BEC from the PDB or ESC (if available).


##### Parts
  * [2 x SG90 Micro Servo 9g](https://www.banggood.com/custlink/DmDRJaGp76) (Optional: for pan/tilt functions)
  * [Foxeer Razer 1.8mm Lens 1200TVL](https://www.banggood.com/custlink/vvDyrOmpu2)
  * [AKK FX337CH 25/200/400/600mW VTX](https://www.banggood.com/custlink/m3mypOGH3m)
  * [SMA 5,8G Antenna](https://www.banggood.com/custlink/vGKRcOGPGc)

##### Wiring

![Arduino And PPM](media/FPV_SERVO.jpg)


### All Parts

All these parts are only suggestions, you will need get the parts that are right for you project.

The 5v BEC is only needed if you are not using a PDB (Power Distribution Board) with an onboard 5v Bec.

  * [Arduino Nano V3](https://www.banggood.com/custlink/mKDyWl3pU3)
  * [STM32 Blackpill](https://www.aliexpress.com/item/4001062944589.html)
  * [Arduino Nano Expansion Board](https://www.banggood.com/custlink/KKmRpavpqC) (optional: For bench testing)
  * [Tiny 2.4G DSM2 6CH PPM Receiver](https://www.banggood.com/custlink/GDmycOmcUI)
  * [Matek Mini PDB With 5V/12V BEC](https://www.banggood.com/custlink/m3vErjGJM1)
  * [5V BEC 2-6S](https://www.banggood.com/custlink/mGDYJO3rfE) (Optional: not needed if PDB/ESC has 5v BEC)
  * [2 x Razor32 35A 3-6S ESC](https://www.banggood.com/custlink/v3GhrL3Jwc)
  * [2 x 2212 930KV 2-4s](https://www.banggood.com/custlink/mK3ypomrfZ)
  * [2200mAh 60C 3S Lipo](https://www.banggood.com/custlink/3GKyWLGtm5)
  * [2 x SG90 Micro Servo 9g](https://www.banggood.com/custlink/DmDRJaGp76) (Optional: for pan/tilt functions)
  * [Foxeer Razer 1.8mm Lens 1200TVL](https://www.banggood.com/custlink/vvDyrOmpu2)
  * [AKK FX337CH 25/200/400/600mW VTX](https://www.banggood.com/custlink/m3mypOGH3m)
  * [SMA 5,8G Antenna](https://www.banggood.com/custlink/vGKRcOGPGc)
  * [Transmitter DSM2 Compatible](https://www.banggood.com/FrSky-Taranis-X-Lite-S-2_4GHz-24CH-ACCST-D16-ACCESS-Mode2-Transmitter-with-PARA-Wireless-Training-Function-and-Quick-charge-System-p-1440288.html) you may also need a DSM [module](https://www.banggood.com/IRangeX-IRX4-LITE-CC2500-NRF24L01+-A7105-CYRF6936-4-IN-1-Multiprotocol-TX-Module-for-Frsky-X-lite-p-1346927.html) 



## How To Contribute

If you would like to contribute contact me and let me know on github or facebook at 

My knowledge of C++ and arduino is limited and I would like to see where this project could go in the future.

If we get enough of a community we can grow the features of Betacrawler.

Follow this project on [Facebook Group](https://www.facebook.com/groups/307432330496662) to ask any questions and any additions you would like to see.


## Resources Used

  - [PPM Reciver](https://github.com/Nikkilae/PPM-reader)
  - [ESC/Servo](https://www.instructables.com/id/ESC-Programming-on-Arduino-Hobbyking-ESC/)
  - [ESC bi-directional](https://www.youtube.com/watch?v=jBr-ZLMt4W4)
  - [ESC PWM Testing](https://github.com/MikeysLab/BrushlessESCviaPWM/blob/master/EscPWMTesting/EscPWMTesting.ino)
  - [Arduino ESC Lib](https://www.robotshop.com/community/blog/show/rc-speed-controller-esc-arduino-library)



