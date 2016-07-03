# Timelapse_ArduCam

This repository contains Arduino code and Eagle design files for a circuit board to interface
with an ArduCam camera module to generate timelapse images. 

![Image of circuit board](/img/Timelapse_ArduCam_RevB.jpg)

Features:
* Images are stored on a micro SD card.
* Timekeeping is accomplished with a DS3231 real time clock chip and backup battery.
* Power is provided by batteries through one of the available power plugs, either a 2.1mm barrel
connector or JST PH series plug (similar to that used on many Lithium battery packs). A 3.3V 
low dropout voltage regulator is included. 
* A reset button and a trigger button are included, and can be mounted on either top or bottom 
of the board. 
* Indicator LEDs may also be mounted on the top or bottom of the board. 
* A FTDI header allows reprogramming of the AVR atmega328P microcontroller.
* A standard ICSP 2x3 header is available for burning a bootloader to the AVR. Alternatively, 
a Tag-Connect TC2030 footprint is available for ICSP functions as well. 

Necessary libraries:
* Arducam: https://github.com/ArduCAM/Arduino
* SdFat: https://github.com/greiman/SdFat
* RTClib: https://github.com/millerlp/RTClib

Written under Arduino 1.6.6