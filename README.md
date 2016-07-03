# Timelapse_ArduCam

This repository contains Arduino code and Eagle design files for a circuit board to interface
with an ArduCam camera module to generate timelapse images. The device will sleep until a 
user defined interval (some number of seconds) is reached, then it will boot up the 
ArduCam module, take one picture, and write it to the SD card before returning to sleep. The
ArduCam module consumes > 100mA while idling, so the AVR microcontroller uses a transistor to
cut off all power to the ArduCam, and puts itself to sleep as well. During sleep intervals the
average overall current draw of the entire device is < 1mA. During an image capture and save 
routine (approx 5-6 seconds), power draw is around 130mA. 

![Image of circuit board](/img/Timelapse_ArduCam_RevB.jpg)

The price for parts and the PCB boards should come out to $75, including the $30 cost of 
of the ArduCam OV2640 2 megapixel camera module
[Amazon link to camera module,](https://www.amazon.com/Arducam-Module-Megapixels-Arduino-Mega2560/dp/B012UXNDOY)
 and the $22 cost to get 3 copies of the circuit board made by OshPark.com.
[direct link to this board on Osh Park](https://oshpark.com/shared_projects/kgNHWteo)



Features:
* Images are stored on a micro SD card.
* Timekeeping is accomplished with a DS3231 real time clock chip and backup battery.
* Power is provided by batteries through one of the available power plugs, either a 2.1mm barrel
connector or JST PH series plug (similar to that used on many Lithium battery packs). A 3.3V 
low dropout voltage regulator is included. I typically use 4xAA batteries to power the unit.
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