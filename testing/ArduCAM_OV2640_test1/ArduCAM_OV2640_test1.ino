/* ArduCAM_OV2640_test1.ino
 *  Modified by LPM
 */  



// ArduCAM demo (C)2014 Lee
// web: http://www.ArduCAM.com
// This program is a demo of how to use most of the functions
// of the library with a supported camera modules, and can run on any Arduino platform.
//
// This demo was made for Omnivision OV2640 2MP sensor.
// It will run the ArduCAM as a real 2MP digital camera, provide both preview and JPEG capture.
// The demo sketch will do the following tasks:
// 1. Set the sensor to BMP preview output mode.
// 2. Switch to JPEG mode when shutter buttom pressed.
// 3. Capture and buffer the image to FIFO. 
// 4. Store the image to Micro SD/TF card with JPEG format.
// 5. Resolution can be changed by myCAM.set_JPEG_size() function.
// This program requires the ArduCAM V3.1.0 (or later) library and Rev.C ArduCAM shield
// and use Arduino IDE 1.5.2 compiler or above

//#include <UTFT_SPI.h>
//#include <SD.h>
#include <SdFat.h>
#include <Wire.h>
#include "ArduCAM.h"
#include <SPI.h>
#include "memorysaver.h"

#if defined(__arm__)
  #include <itoa.h>
#endif

// Define the CS pin for the SD card 
#define SD_CS 9    // arduino pin 9, avr pin PB1
#define SPI_CS 10  // arduino pin 10, avr pin PB2
#define GRNLED PD4
#define REDLED PD3
#define BUTTON1 2

// set pin 10 as the slave select for SPI:
//const int SPI_CS = 10;

// Create sd objects
SdFat sd;
SdFile logfile;  // for sd card, this is the file object to be written to

ArduCAM myCAM(OV2640,SPI_CS);
//UTFT myGLCD(SPI_CS);
boolean isShowFlag = false;

//**************************************
void setup()
{
  pinMode(GRNLED, OUTPUT);
  pinMode(REDLED, OUTPUT);
  pinMode(SPI_CS, OUTPUT);
  
  // Define a trigger pin (connect to ground to trigger)
  pinMode(BUTTON1, INPUT_PULLUP);
  
  uint8_t vid,pid;
  uint8_t temp; 
#if defined(__SAM3X8E__)
  Wire1.begin();
#else
  Wire.begin();
#endif
  Serial.begin(57600);
  Serial.println("ArduCAM Start!"); 
  // set the SPI_CS as an output:
  pinMode(SPI_CS, OUTPUT);

  // initialize SPI:
  SPI.begin(); 
  //Check if the ArduCAM SPI bus is OK
  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  temp = myCAM.read_reg(ARDUCHIP_TEST1);
  if(temp != 0x55)
  {
    Serial.println("SPI interface Error!");
    while(1);
  }
  
  //Change MCU mode
  // MCU2LCD_MODE denotes that microcontroller is responsible
  // for interfacing with a LCD display screen, rather than 
  // the ArduCAM chip
  myCAM.set_mode(MCU2LCD_MODE);

  
  //Check if the camera module type is OV2640
  myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
  if((vid != 0x26) || (pid != 0x42))
    Serial.println("Can't find OV2640 module!");
  else
    Serial.println("OV2640 detected");
    
  //Change to BMP capture mode and initialize the OV2640 module     
  myCAM.set_format(BMP);

  myCAM.InitCAM();
  
//  //Initialize SD Card
//  if (!SD.begin(SD_CS)) 
//  {
//    //while (1);    //If failed, stop here
//    Serial.println("SD Card Error");
//  }
//  else
//    Serial.println("SD Card detected!");

 	// Initialize the SD card object
	// Try SPI_FULL_SPEED, or SPI_HALF_SPEED if full speed produces
	// errors on a breadboard setup. 
if (!sd.begin(SD_CS, SPI_FULL_SPEED)) {
	// If the above statement returns FALSE after trying to 
	// initialize the card, enter into this section and
	// hold in an infinite loop.
  Serial.println(F("SD card error"));
  while(1){ // infinite loop due to SD card initialization error
          
          digitalWrite(REDLED, HIGH);
          delay(100);
          digitalWrite(REDLED, LOW);
          digitalWrite(GRNLED, HIGH);
          delay(100);
          digitalWrite(GRNLED, LOW);
  	  }
} else {
 Serial.println(F("SD detected"));
} 
    
    
    
    
}


//*************************************************
void loop()
{
  char str[8];
  File outFile;
  byte buf[256];
  static int i = 0;
  static int k = 0;
  static int n = 0;
  uint8_t temp,temp_last;
  uint8_t start_capture = 0;
  int total_time = 0;

  //Wait trigger from shutter button 
  if ( digitalRead(BUTTON1) == 0)  
  {
    Serial.println(F("Trigger"));
    isShowFlag = false;
    myCAM.set_mode(MCU2LCD_MODE);
    myCAM.set_format(JPEG);
    myCAM.InitCAM();

//    myCAM.OV2640_set_JPEG_size(OV2640_640x480);
    myCAM.OV2640_set_JPEG_size(OV2640_1600x1200);
    //Wait until button released
    while(digitalRead(BUTTON1) == 0);  
    delay(1000);
    start_capture = 1;
    Serial.println("start")  ;
  }

  if(start_capture)
  {
    //Flush the FIFO 
    myCAM.flush_fifo(); 
    //Clear the capture done flag 
    myCAM.clear_fifo_flag();     
    //Start capture
    myCAM.start_capture();
    Serial.println(F("Start Capture"));     
  }
  
  if(myCAM.get_bit(ARDUCHIP_TRIG ,CAP_DONE_MASK))
  {

    Serial.println(F("Capture Done"));
    
    //Construct a file name
    k = k + 1;
    itoa(k, str, 10); 
    strcat(str,".jpg");
    //Open the new file  
    outFile = sd.open(str,O_WRITE | O_CREAT | O_TRUNC);
    if (! outFile) 
    { 
      Serial.println(F("Open file failed"));
      return;
    }
    total_time = millis();
    i = 0;
    temp = myCAM.read_fifo();
    //Write first image data to buffer
    buf[i++] = temp;

    //Read JPEG data from FIFO
    while( (temp != 0xD9) | (temp_last != 0xFF) )
    {
      temp_last = temp;
      temp = myCAM.read_fifo();
      //Write image data to buffer if not full
      if(i < 256)
        buf[i++] = temp;
      else
      {
        //Write 256 bytes image data to file on SD card
        outFile.write(buf,256);
        i = 0;
        buf[i++] = temp;
      }
    }
    //Write the remain bytes in the buffer
    if(i > 0)
      outFile.write(buf,i);

    //Close the file 
    outFile.close(); 
    total_time = millis() - total_time;
    Serial.print(F("Total time used:"));
    Serial.print(total_time, DEC);
    Serial.println(F(" millisecond"));    
    //Clear the capture done flag 
    myCAM.clear_fifo_flag();
    //Clear the start capture flag
    start_capture = 0;
    
    myCAM.set_format(BMP);
    myCAM.InitCAM();
    isShowFlag = false;  
  }
}

   


