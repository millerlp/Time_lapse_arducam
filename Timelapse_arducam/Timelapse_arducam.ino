/* Timelapse_cam_test3.ino
 *  LPM 2016
 *  
 *  Made for RevB of Timelapse Arducam board
 *  RevB contains a NPN transistor to shut down the
 *  Arducam module. 
 *  Time is provided by a DS3231M RTC chip. 
 *  
 *  Currently functional, uses roughly 140mA when 
 *  camera is on, but drops to <1mA when camera is
 *  completely shut down. 
 */  


#include <SdFat.h>  // https://github.com/greiman/SdFat
#include <Wire.h>
#include "ArduCAM.h"  // https://github.com/ArduCAM/Arduino
#include <SPI.h>
#include "memorysaver.h"  // https://github.com/ArduCAM/Arduino
#include "RTClib.h"   // https://github.com/millerlp/RTClib

/* NOTE - when first setting up the ArduCAM library,
*	you will need to open the memorysaver.h file and
*	uncomment the #define line that matches your
*	particular sensor. The default is the OV2640_CAM. 
*/
// The following libraries should come with the normal Arduino 
// distribution. 
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/atomic.h>
#include <wiring_private.h>
#include <avr/wdt.h>



// Define the CS pin for the SD card 
#define SD_CS 9    // arduino pin 9, avr pin PB1
#define SPI_CS 10  // Arducam CS, arduino pin 10, avr pin PB2
#define REDLED 3  // Red LED
#define GRNLED 4  // Green LED
#define BUTTON1 2   // Tactile switch (INT0)
#define NPN 7 // Base pin of NPN transistor

// Define the sleep/wake cycle (seconds)
#define SAMPLES_PER_SECOND 1

// Create SD card objects
SdFat sd;
SdFile outFile;  // for sd card, this is the file object to be written to

// Create real time clock object
RTC_DS3231 rtc; 
DateTime myTime;  // variable to keep time

// ArduCam variables
ArduCAM myCAM(OV2640,SPI_CS); // Defines camera module type and chip select pin
uint8_t start_capture = 0;
int total_time = 0;

int dawnTime = 5;	// Specify hour before which no pictures should be taken
int duskTime = 21; 	// Specify hour after which no pictures should be taken
int Interval = 15; // Specify a seconds mark to take picture on (10, 15, 20, 30)
                  // A normal picture+save operation takes ~ 5-6 seconds
int cameraWarmUpTime = 500; // (ms) Delay to let camera stabilize after waking

// Declare initial name for output files written to SD card
// The newer versions of SdFat library support long filenames
char filename[] = "YYYYMMDD_HHMMSS.JPG";

byte intFlag = 0; // Flag to keep track of interrupts



//**************************************
void setup()
{
  pinMode(GRNLED, OUTPUT);
  pinMode(REDLED, OUTPUT);
  pinMode(SPI_CS, OUTPUT);
  pinMode(NPN, OUTPUT);
  digitalWrite(NPN, HIGH); // HIGH turns on Arducam
  
  // Define a trigger pin (connect to ground to trigger)
  pinMode(BUTTON1, INPUT_PULLUP);
  
  uint8_t vid,pid;
  uint8_t temp; 

  // Flash Green LED to show reboot happened
  digitalWrite(GRNLED, HIGH);
  delay(1000);
  digitalWrite(GRNLED, LOW);


  Serial.begin(57600);
  Serial.println(F("Reboot"));
  
  rtc.begin();
  myTime = rtc.now();
  char buf1[25];
  myTime.toString(buf1, 25);
  Serial.println(buf1);
  // Check the year to see if it's sensible. If not, 
  // notify the useR by flashing the red led a bunch.
   if (myTime.year() < 2016 | myTime.year() >= 2164){
      for (int j = 0; j < 30; j++){ 
          digitalWrite(REDLED, HIGH);
          delay(400);
          digitalWrite(REDLED, LOW);
          delay(400);
          Serial.println(F("RTC error"));
      }
   }



  // initialize SPI:
  SPI.begin(); 
  
  //***********************************
  // Initialize Arducam modules
  // Clear the power down bit in case the device
  // was left in power-down mode (re-enables ArduCAM module)
  myCAM.clear_bit(ARDUCHIP_GPIO,GPIO_PWDN_MASK);
  // Re-enable the memory controller circuit
	myCAM.clear_bit(0x83, FIFO_PWRDN_MASK);
	myCAM.clear_bit(0x83, LOW_POWER_MODE); 
  
  //Check if the ArduCAM SPI bus is OK
  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  temp = myCAM.read_reg(ARDUCHIP_TEST1);
  if(temp != 0x55)
  {
    Serial.println("SPI interface Error!");
    while(1);
  }
  
  // Change MCU mode
  // MCU2LCD_MODE denotes that microcontroller is responsible
  // for interfacing with a LCD display screen, rather than 
  // the ArduCAM chip
  myCAM.set_mode(MCU2LCD_MODE);
  
  //Check if the camera module type is OV2640
  myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
  if((vid != 0x26) || (pid != 0x42)) {
    Serial.println("Can't find OV2640 module!");
    while(1){ // infinite loop due to SD card initialization error
            
            digitalWrite(REDLED, HIGH);
            delay(300);
            digitalWrite(REDLED, LOW);
            digitalWrite(GRNLED, HIGH);
            delay(300);
            digitalWrite(GRNLED, LOW);
    	  }
  } else {
    Serial.println("OV2640 detected");
  }
    
  //Change to JPG capture mode and initialize the OV2640 module     
  myCAM.set_format(JPEG);

  myCAM.InitCAM();
  
  // Set the camera module power down bit (enters power down mode)
  myCAM.set_bit(ARDUCHIP_GPIO,GPIO_PWDN_MASK);
  
  //**************************************
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
		Serial.println(F("SD init"));
  }

  //*******************************************
  // Power down the Arducam module and set all of its
  // pins to inputs in order to stop any parasitic power
  // draw
  SPI.end();
  digitalWrite(13, LOW);
  pinMode(13, INPUT);   // SPI SCK
  pinMode(12, INPUT);   // SPI MISO   
  pinMode(11, INPUT);   // SPI MOSI
  pinMode(10, INPUT);   // SPI Chip select for Arducam
  // Shut down I2C (Wire) functions temporarily
//  Wire.end();
  pinMode(SDA, INPUT);  // I2C SDA line
  pinMode(SCL, INPUT);  // I2C SCL line
  digitalWrite(NPN, LOW); // disconnect Arducam's ground pin

  
  startTIMER2(myTime);
    
} // end of setup() loop
//*************************************************

//*************************************************
void loop()
{
	// Check time at start of every loop
  Wire.begin();
	myTime = rtc.now();
	char buf1[25];
	myTime.toString(buf1, 25);
	Serial.println(buf1);
	delay(5);
//  Wire.end();
  pinMode(SDA, INPUT);
  pinMode(SCL, INPUT);

  // Only take picture during daylight hours
  if (myTime.hour() >= dawnTime & myTime.hour() <= duskTime) {
    // Take a picture every Interval seconds
    if (myTime.second() % Interval == 0){
      total_time = millis(); // store time used to take and store picture 
      // Call enableArduCam function to turn the module back on
      enableArduCam();
      start_capture = 1;
      //Flush the FIFO 
      myCAM.flush_fifo(); 
      //Clear the capture done flag before starting new capture
      myCAM.clear_fifo_flag();   

      digitalWrite(GRNLED, HIGH);
      delay(cameraWarmUpTime); // ?Let camera exposure stabilize before picture?  
      digitalWrite(GRNLED, LOW);
      
      //Start capture
      myCAM.start_capture();    
      // Check the Capture Done flag, which is stored in the ARDUCHIP_TRIG
      // register for some reason.
      while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
        // do nothing while waiting for capture done flag
      }
  
      if(myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
         Serial.println(F("Capture Done"));
    
        // Update the time stamp
//         Wire.begin();
         myTime = rtc.now();
        //Construct a file name
        initFileName(myTime);
        // Open a new file on the SD card with the filename
        if (!outFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
          // If file cannot be created, raise an error
          Serial.println(F("SD card error"));
          digitalWrite(REDLED, HIGH); // turn on error LED
        }
        // Transfer data from ArduCam to the SD card
        writeToSD(myTime);
        
        total_time = millis() - total_time;
        Serial.print(F("Total time used:"));
        Serial.print(total_time, DEC);
        Serial.println(F(" ms")); 
        digitalWrite(GRNLED,HIGH);
        delay(15); // Give Serial time to write   
        digitalWrite(GRNLED,LOW);
        
        myCAM.flush_fifo(); // force another flush
        // Power down the ArduCam (this disables I2C and SPI also)
        disableArduCam();
      } // end of if(myCAM.get_bit(ARDUCHIP_TRIG ,CAP_DONE_MASK))
    } // end of if (myTime.second() % 30 == 0)
  } // end of if (myTime.hour() >= dawnTime & myTime.hour() <= duskTime) 
  // At this point a picture has been take and stored if the code above
  // executed.

  // Alternatively, recognize a button trigger push, and take a picture
  // manually
  if ( digitalRead(BUTTON1) == 0)  
  {
    Serial.println(F("Trigger"));
    digitalWrite(REDLED, HIGH);
    delay(40);
    digitalWrite(REDLED, LOW);

    total_time = millis(); // store time used to take and store picture 
    // Call enableArduCam function to turn the module back on
    enableArduCam();
      
    //Wait until button released
  	digitalWrite(REDLED, HIGH);
      while(digitalRead(BUTTON1) == 0);  
      delay(10);
  	digitalWrite(REDLED, LOW);
      start_capture = 1;
  }

  if(start_capture)
  {
    //Flush the FIFO 
    myCAM.flush_fifo();   // SPI bus
    //Clear the capture done flag before starting new capture
    myCAM.clear_fifo_flag();   
    
    delay(cameraWarmUpTime); // ?Let camera exposure stabilize before picture?  
    
    //Start capture
    myCAM.start_capture();    
	  // Check the Capture Done flag, which is stored in the ARDUCHIP_TRIG
	  // register for some reason.
	while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
	  // do nothing while waiting for capture done flag
	}
	
	if(myCAM.get_bit(ARDUCHIP_TRIG ,CAP_DONE_MASK)) {
		 Serial.println(F("Capture Done"));

    // Update the time stamp
//     Wire.begin();
     myTime = rtc.now();
		//Construct a file name
		initFileName(myTime);
		// Open a new file on the SD card with the filename
		if (!outFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
			// If file cannot be created, raise an error
			Serial.println(F("SD card error"));
			digitalWrite(REDLED, HIGH); // turn on error LED
		}
    // Transfer data from ArduCam to the SD card
    writeToSD(myTime);
		
		total_time = millis() - total_time;
		Serial.print(F("Total time used:"));
		Serial.print(total_time, DEC);
		Serial.println(F(" ms"));
    delay(5); // Give Serial time to write    

    // Power down the ArduCam (this disables I2C and SPI also)
    disableArduCam();
   
		digitalWrite(REDLED, HIGH);
		delay(800);
		digitalWrite(REDLED, LOW);

	  }
  } // End of manual trigger mode
  
  	// Put the AVR to sleep
	goToSleep();
	
} // end of main loop()

//-----------------------------------------------------------------------------
// This Interrupt Service Routine (ISR) is called every time the
// TIMER2_OVF_vect goes high (==1), which happens when TIMER2
// overflows. The ISR doesn't care if the AVR is awake or
// in SLEEP_MODE_PWR_SAVE, it will still roll over and run this
// routine. If the AVR is in SLEEP_MODE_PWR_SAVE, the TIMER2
// interrupt will also reawaken it. This is used for the goToSleep() function
ISR(TIMER2_OVF_vect) {
	if (intFlag == 0) { // if flag is 0 when interrupt is called
		intFlag = 1; // set the flag to 1
	} 
}

//------------enableArduCam--------------------------------------------------
// A function to wake and intialize the ArduCam. This is in a function
// just to make the code in the main loop more compact and readable
void enableArduCam (void) 
{
  uint8_t temp;
  Wire.begin();
  SPI.begin();
  digitalWrite(NPN, HIGH); // power up Arducam supply
  
  // Time to power stuff back up and take a picture
  
    // Initialize the SD card object
    // Try SPI_FULL_SPEED, or SPI_HALF_SPEED if full speed produces
    // errors on a breadboard setup. 
    pinMode(SD_CS, OUTPUT); 
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
      Serial.println(F("SD init"));
    } 
  
  // Renable the ArduCAM module
  
  pinMode(SPI_CS, OUTPUT);

    //Check if the ArduCAM SPI bus is OK
  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  temp = myCAM.read_reg(ARDUCHIP_TEST1);
  if(temp != 0x55)
  {
    Serial.println(F("SPI interface Error!"));
    while(1);
  } else {
    Serial.println(F("Arducam OK"));
  }
  
  // Re-enable the sensor module (all settings will be lost)
  myCAM.clear_bit(ARDUCHIP_GPIO, GPIO_PWDN_MASK);
  // Re-enable the memory controller circuit
  // myCAM.clear_bit(0x83, FIFO_PWRDN_MASK);
  myCAM.clear_bit(0x83, LOW_POWER_MODE); // alternate attempt at memory power up

  // Reset all of the sensor module settings
    myCAM.set_mode(MCU2LCD_MODE);
    myCAM.set_format(JPEG);
    myCAM.InitCAM();
    myCAM.OV2640_set_JPEG_size(OV2640_1600x1200);    
} // end of enableArduCam function

//-------------writeToSD--------------------
// Function to transfer data from Arducam buffer to 
// a file on SD card
void writeToSD(DateTime myTime)
{
  byte buf[256]; // used for moving image data to sd card
  static int i = 0; // used to keep track of image data saving
  uint8_t temp = 0;
  uint8_t temp_last = 0; // used to keep track of image data saving

 //************************************ 
  bool nostart = true;
  int fifomarker = 0;
  uint8_t store1 = 0;
  uint8_t store2 = 0;
  uint8_t store3 = 0;
  while (nostart) {
    store3 = store2;
    store2 = store1;
    store1 = myCAM.read_fifo(); // Get 1 byte from buffer
    fifomarker++; // Increment to mark that one more byte has been read
    Serial.print(fifomarker);
    Serial.print("\t");
    Serial.print(store3, HEX);
    Serial.print(" ");
    Serial.print(store2, HEX);
    Serial.print(" ");
    Serial.println(store1, HEX);
    
    // Check to see if the last three bytes read are FF D8 FF
    if ( (store3 == 0xFF) & (store2 == 0xD8) & (store1 == 0xFF) ){
       nostart = false; // Will kill the while loop
       // Once the match to FF D8 FF is found, set the fifomarker
       // back to the index (0-based) that marks the start of the
       // FF D8 FF sequence in the fifo buffer
       fifomarker = fifomarker - 3; 
       Serial.print("Found marker ");
       Serial.println(fifomarker);
       
    }
  }
  
  Serial.print(myCAM.read_fifo(), HEX);
  Serial.print(" ");
  Serial.print(myCAM.read_fifo(), HEX);
  Serial.print(" ");
  Serial.print(myCAM.read_fifo(), HEX);
  Serial.println();
  
  // Reset FIFO read pointer to 0. The FIFO_RDPTR_RST_MASK is
  // defined as 0x10 (setting bit 3) in Arducam.h, although the 
  // hardware guide says that the read pointer reset is at bit 4
  // (0x20) and the write pointer reset is at bit 5 (0x40)
  myCAM.write_reg(ARDUCHIP_FIFO, 0x10); // prepending cruft
  // Sending 0x10 is probably resetting the read_fifo pointer 
  // back to 0. The first file comes through fine. The 2nd file
  // has random bytes at start, then the full FF D8 FF code and 
  // JFIF. Based on this test and the 0x20 test below, I think the
  // random bytes aren't actually coming from the FIFO buffer, since
  // it always returns FF D8 FF as the first three bytes on every 
  // picture (1st, 2nd, 3rd pic etc in the loop). 
  
  // The culprit of the random bytes must be coming from somewhere
  // else in the pipeline, after the FIFO buffer. If the SPI 
  // buffer somehow has random chunk retained in it, this could 
  // be an issue, and the SdFat library may be to blame. Check if there's
  // a way to fully flush the SdFat buffers after writing and closing 
  // the file. 


//  myCAM.write_reg(ARDUCHIP_FIFO, 0x20); // prepending cruft
  // Using 0x20 results in the first (usually good file) having
  // the FF D8 FF cut off, starting then with JFIF. 2nd file
  // has many random bytes at start, then FF D8 FF is cut off, and
  // then JFIF appears. So this is probably not resetting the
  // read_fifo pointer back to 0. 3 values of the fifo were read 
  // out (the good FF D8 FF), and then not recovered when 
  // fifomarker is reset, since the pointer isn't reset. 
  
  
  // Now that we're back at the start of the FIFO, advance the
  // specified number of bytes to get to the real start of the
  // JPEG file. This loop will just read individual bytes until
  // enough have been read to reach the starting code FF D8 FF
  for (int j = fifomarker; j > 0; j--){
     Serial.print(j);Serial.println(" Dummy read");
     temp = myCAM.read_fifo(); // dummy read
  }
  
  //************************************
  
  temp = myCAM.read_fifo(); // Get 1 byte from camera fifo buffer
  // Write first image data to buffer
  buf[i++] = temp;


  // Read JPEG data from FIFO
  // JPEGs should end with FF D9 (hex), so this
  // statement looks for that code to determine whether 
  // this is the end of the file or not. 
  while( (temp != 0xD9) | (temp_last != 0xFF) )
  {
    temp_last = temp;
    temp = myCAM.read_fifo(); // read another byte
      // Write image data to buffer if not full
      if (i < 256){
      buf[i++] = temp;
    } else {
      // Write 256 bytes image data to file on SD card
      outFile.write(buf,256);
      i = 0;
      buf[i++] = temp;
    }
  }
  // Write the remaining bytes in the buffer since the
  // previous while statement has been satisfied now
  if (i > 0) {
    outFile.write(buf, i);
  }
  
  // Update the file's creation date, modify date, and access date.
  outFile.timestamp(T_CREATE, myTime.year(), myTime.month(), myTime.day(), 
      myTime.hour(), myTime.minute(), myTime.second());
  outFile.timestamp(T_WRITE, myTime.year(), myTime.month(), myTime.day(), 
      myTime.hour(), myTime.minute(), myTime.second());
  outFile.timestamp(T_ACCESS, myTime.year(), myTime.month(), myTime.day(), 
      myTime.hour(), myTime.minute(), myTime.second());
  //Close the file 
  outFile.close(); 
}

//----------------disableArduCam----------------
// Function to shut ArduCam module down completely
void disableArduCam(void)
{
    // Clear the capture done flag 
    myCAM.clear_fifo_flag();
    // Clear the start capture flag
    start_capture = 0;
    
    // Power down the memory controller circuit   
//    myCAM.set_bit(0x83, LOW_POWER_MODE); // alternate attempt at memory shutdown
    // Power down the camera module. This will lose all settings on 
    // the camera (resolution, file format etc.)
//    myCAM.set_bit(ARDUCHIP_GPIO,GPIO_PWDN_MASK);  //enable low power
    
    // Shut down the SPI lines, turn outputs into INPUT to prevent
    // parasitic power loss
    SPI.end();
    digitalWrite(13, LOW);
    pinMode(13, INPUT);  
    pinMode(12, INPUT);
    pinMode(11, INPUT);
    pinMode(10, INPUT);
//    Wire.end();
    pinMode(SDA, INPUT);
    pinMode(SCL, INPUT);
    // Kill the Arducam's power supply
    digitalWrite(NPN, LOW); 
}


//--------------------------------------------------------------
// startTIMER2 function
// Starts the 32.768kHz clock signal being fed into XTAL1 to drive the
// quarter-second interrupts used during data-collecting periods. 
// Supply a current DateTime time value. 
// This function returns a DateTime value that can be used to show the 
// current time when returning from this function. 
DateTime startTIMER2(DateTime currTime){
  TIMSK2 = 0; // stop timer 2 interrupts

  rtc.enable32kHz(true);
  ASSR = _BV(EXCLK); // Set EXCLK external clock bit in ASSR register
  // The EXCLK bit should only be set if you're trying to feed the
  // 32.768 clock signal from the Chronodot into XTAL1. 

  ASSR = ASSR | _BV(AS2); // Set the AS2 bit, using | (OR) to avoid
  // clobbering the EXCLK bit that might already be set. This tells 
  // TIMER2 to take its clock signal from XTAL1/2
  TCCR2A = 0; //override arduino settings, ensure WGM mode 0 (normal mode)
  
  // Set up TCCR2B register (Timer Counter Control Register 2 B) to use the 
  // desired prescaler on the external 32.768kHz clock signal. Depending on 
  // which bits you set high among CS22, CS21, and CS20, different 
  // prescalers will be used. See Table 18-9 on page 158 of the AVR 328P 
  // datasheet.
  //  TCCR2B = 0;  // No clock source (Timer/Counter2 stopped)
  // no prescaler -- TCNT2 will overflow once every 0.007813 seconds (128Hz)
  //  TCCR2B = _BV(CS20) ; 
  // prescaler clk/8 -- TCNT2 will overflow once every 0.0625 seconds (16Hz)
  //  TCCR2B = _BV(CS21) ; 
#if SAMPLES_PER_SECOND == 4
  // prescaler clk/32 -- TCNT2 will overflow once every 0.25 seconds
  TCCR2B = _BV(CS21) | _BV(CS20); 
#endif

#if SAMPLES_PER_SECOND == 2
  TCCR2B = _BV(CS22) ; // prescaler clk/64 -- TCNT2 will overflow once every 0.5 seconds
#endif

#if SAMPLES_PER_SECOND == 1
    TCCR2B = _BV(CS22) | _BV(CS20); // prescaler clk/128 -- TCNT2 will overflow once every 1 seconds
#endif

  // Pause briefly to let the RTC roll over a new second
  DateTime starttime = currTime;
  // Cycle in a while loop until the RTC's seconds value updates
  while (starttime.second() == currTime.second()) {
    delay(1);
    currTime = rtc.now(); // check time again
  }

  TCNT2 = 0; // start the timer at zero
  // wait for the registers to be updated
  while (ASSR & (_BV(TCN2UB) | _BV(TCR2AUB) | _BV(TCR2BUB))) {} 
  TIFR2 = _BV(OCF2B) | _BV(OCF2A) | _BV(TOV2); // clear the interrupt flags
  TIMSK2 = _BV(TOIE2); // enable the TIMER2 interrupt on overflow
  // TIMER2 will now create an interrupt every time it rolls over,
  // which should be every 0.25, 0.5 or 1 seconds (depending on value 
  // of SAMPLES_PER_SECOND) regardless of whether the AVR is awake or asleep.
//  mainState = STATE_IDLE;
  return currTime;
}
   

// ------------ initFileName --------------------------------
// Function to generate a new output file name based on the 
// current date and time from the real time clock.
// The character array 'filename' needs to be declared as a 
// global variable at the top of the program, and this function
// will overwrite portions of it with the new date/time info
void initFileName(DateTime time1) {
	char buf[5];
	// integer to ascii function itoa(), supplied with numeric year value,
	// a buffer to hold output, and the base for the conversion (base 10 here)
	itoa(time1.year(), buf, 10);
	// copy the ascii year into the filename array
	for (byte i = 0; i <= 4; i++){
		filename[i] = buf[i];
	}
	// Insert the month value
	if (time1.month() < 10) {
		filename[4] = '0';
		filename[5] = time1.month() + '0';
	} else if (time1.month() >= 10) {
		filename[4] = (time1.month() / 10) + '0';
		filename[5] = (time1.month() % 10) + '0';
	}
	// Insert the day value
	if (time1.day() < 10) {
		filename[6] = '0';
		filename[7] = time1.day() + '0';
	} else if (time1.day() >= 10) {
		filename[6] = (time1.day() / 10) + '0';
		filename[7] = (time1.day() % 10) + '0';
	}
	// Insert an underscore between date and time
	filename[8] = '_';
	// Insert the hour
	if (time1.hour() < 10) {
		filename[9] = '0';
		filename[10] = time1.hour() + '0';
	} else if (time1.hour() >= 10) {
		filename[9] = (time1.hour() / 10) + '0';
		filename[10] = (time1.hour() % 10) + '0';
	}
	// Insert minutes
		if (time1.minute() < 10) {
		filename[11] = '0';
		filename[12] = time1.minute() + '0';
	} else if (time1.minute() >= 10) {
		filename[11] = (time1.minute() / 10) + '0';
		filename[12] = (time1.minute() % 10) + '0';
	}
	// Insert seconds
	if (time1.second() < 10) {
		filename[13] = '0';
		filename[14] = time1.second() + '0';
	} else if (time1.second() >= 10) {
		filename[13] = (time1.second() / 10) + '0';
		filename[14] = (time1.second() % 10) + '0';
	}
	
}

//--------------goToSleep--------------------------------------------------
// goToSleep function. When called, this puts the AVR to
// sleep until it is awakened by an interrupt (TIMER2 in this case)
// This is a higher power sleep mode than the lowPowerSleep function uses.
void goToSleep()
{
	// Create three variables to hold the current status register contents
	byte adcsra, mcucr1, mcucr2;
	// Cannot re-enter sleep mode within one TOSC cycle. 
	// This provides the needed delay.
	OCR2A = 0; // write to OCR2A, we're not using it, but no matter
	while (ASSR & _BV(OCR2AUB)) {} // wait for OCR2A to be updated
	// Set the sleep mode to PWR_SAVE, which allows TIMER2 to wake the AVR
	set_sleep_mode(SLEEP_MODE_PWR_SAVE);
	adcsra = ADCSRA; // save the ADC Control and Status Register A
	ADCSRA = 0; // disable ADC
	sleep_enable();
	// Do not interrupt before we go to sleep, or the
	// ISR will detach interrupts and we won't wake.
	noInterrupts ();
	
	wdt_disable(); // turn off the watchdog timer
	
	//ATOMIC_FORCEON ensures interrupts are enabled so we can wake up again
	ATOMIC_BLOCK(ATOMIC_FORCEON) { 
		// Turn off the brown-out detector
		mcucr1 = MCUCR | _BV(BODS) | _BV(BODSE); 
		mcucr2 = mcucr1 & ~_BV(BODSE);
		MCUCR = mcucr1; //timed sequence
		// BODS stays active for 3 cycles, sleep instruction must be executed 
		// while it's active
		MCUCR = mcucr2; 
	}
	// We are guaranteed that the sleep_cpu call will be done
	// as the processor executes the next instruction after
	// interrupts are turned on.
	interrupts ();  // one cycle, re-enables interrupts
	sleep_cpu(); //go to sleep
	//wake up here
	sleep_disable(); // upon wakeup (due to interrupt), AVR resumes here

}

//-----------------------------------------------------------------------------
// lowPowerSleep function
// This sleep version uses the watchdog timer to sleep for 0.5 seconds at a time

void lowPowerSleep(void){

  /* It seems to be necessary to zero out the Asynchronous clock status 
   * register (ASSR) before enabling the watchdog timer interrupts in this
   * process. 
   */
  ASSR = 0;  
  TIMSK2 = 0; // stop timer 2 interrupts
  // Cannot re-enter sleep mode within one TOSC cycle. 
  // This provides the needed delay.
  OCR2A = 0; // write to OCR2A, we're not using it, but no matter
  while (ASSR & _BV(OCR2AUB)) {} // wait for OCR2A to be updated

  ADCSRA = 0;   // disable ADC
  set_sleep_mode (SLEEP_MODE_PWR_DOWN);  // specify sleep mode
  sleep_enable();
  // Do not interrupt before we go to sleep, or the
  // ISR will detach interrupts and we won't wake.
  noInterrupts ();
  //--------------------------------------------------------------------------
  // Set up Watchdog timer for long term sleep

  // Clear the reset flag first
  MCUSR &= ~(1 << WDRF);

  // In order to change WDE or the prescaler, we need to
  // set WDCE (This will allow updates for 4 clock cycles).
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  // Enable watchdog interrupt (WDIE), and set 8 second delay
  // WDTCSR = bit(WDIE) | bit(WDP3) | bit(WDP0); 
  // Enable watchdog interrupt (WDIE), and set 0.5 second delay
  WDTCSR = bit(WDIE) | bit(WDP2) | bit(WDP0) ;
  wdt_reset();

  // Turn off brown-out enable in software
  // BODS must be set to one and BODSE must be set to zero within four clock 
  // cycles, see section 10.11.2 of 328P datasheet
  MCUCR = bit (BODS) | bit (BODSE);
  // The BODS bit is automatically cleared after three clock cycles
  MCUCR = bit (BODS);
  // We are guaranteed that the sleep_cpu call will be done
  // as the processor executes the next instruction after
  // interrupts are turned on.
  interrupts ();  // one cycle, re-enables interrupts
  sleep_cpu ();   // one cycle, going to sleep now, wake on interrupt
  // The AVR is now asleep. In SLEEP_MODE_PWR_DOWN it will only wake
  // when the watchdog timer counter rolls over and creates an interrupt
  //-------------------------------------------------------------------
  // disable sleep as a precaution after waking
  sleep_disable();
}
