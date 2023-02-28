#include "PMS.h"
#include "SoftwareSerial.h"
#include <TimeLib.h>


// BASIC VARIABLE SETUP ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Serial is for printing.
// SerialPMS communicates with PMS Sensor.
// Both are 9600 baud.
SoftwareSerial SerialPMS(2, 3);
PMS pms(SerialPMS);

// PMS_READ_INTERVAL (40 sec) and PMS_READ_DELAY (20 sec) define, respectively, the time between sleep and wake, and the time between wake and read.
// THEY CAN'T BE EQUAL, because their lengths are used to detect sensor state.
// Passed initial is a boolean flag, determining the initial time gap to sync on the dot with the hour.
static const uint32_t PMS_READ_INTERVAL = 40000;
static const uint32_t PMS_READ_DELAY = 20000;

static bool passedInitialOffset = false;
static unsigned long lastTime = 0;

// This variable, timerInterval, is the crux of the code. 
// It alternates between the values of PMS_READ_INTERVAL and PMS_READ_DELAY to determine the state of the code, except for at the start, where it becomes initialReadOffset. 
uint32_t timerInterval = PMS_READ_DELAY; 

// Timing stuff
#define TIME_HEADER "T"  // Header tag for serial time sync message
#define TIME_REQUEST 7   // ASCII bell character requests a time sync message

// setup() ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void setup()
{
  SerialPMS.begin(9600); 
  Serial.begin(9600);
  setSyncProvider(requestSync);

  // For time sync off the get-go
  requestSync();
  processSyncMessage();
  
  // Switch to passive mode.
  pms.passiveMode();
  
  // Default state after sensor power, but undefined after ESP restart e.g. by OTA flash, so we have to manually wake up the sensor for sure.
  // Some logs from bootloader is sent via Serial port to the sensor after power up. This can cause invalid first read or wake up so be patient and wait for next read cycle.
  // Sean -- I don't think these two comments are relevant because the demo code was using a different board called ESP, but I kept them in case
  pms.wakeUp();

  /*
  Explanation for initial time offset:

  -initialReadOffset represents the time between the current time, at this point after having setup, woken, etc., and the first second of the next minute.
  -timerInterval is set to initialReadOffset to make sure that the very first iteration of loop() delays for that long.
     -Since only timerInterval is changed, the two delay constants, PMS_READ_INTERVAL and PMS_READ_DELAY, are never changed.
     -After the very first iteration, timerInterval is changed to PMS_READ_DELAY, signifying wake-read delay, and business continues as usual with the else statement conditional.
       -The quality of timerInterval being equal to initialReadOffset is very specifically detected with a boolean flag:
       -This circumvents the direct jump to PMS_INTERVAL_DELAY which would occur if PMS_READ_DELAY and initialReadOffset were coincidentally equal.
  -lastTime is set (initially) to the current time so as to provide the most accurate interval from initialReadOffset.
  -!!!IMPORTANT. It is vital that this code is at the END of setup because it gives the latest possible lastTime, in other words, is closest to loop(). This gives it the most time accuracy.
     -Even putting this code before passiveMode() and wakeUp() gives a 1 second offset error!
  */

  Serial.print("Initial ");
  displayTime();
  unsigned int initialReadOffset = getInitialReadOffset();
  Serial.println(String(initialReadOffset));
  timerInterval = initialReadOffset;
  lastTime = millis();

}

unsigned int getInitialReadOffset() {
  if (second() <= 40) {
    return ((60 - second()) * 1000) - PMS_READ_DELAY;
  } else {
    return (120 - (PMS_READ_DELAY/1000) - second())*1000;
  }
}

// loop() and callback() ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void loop()
{  
  // Time Stuff
  if (Serial.available()) {
    processSyncMessage();
  }

  unsigned long currentTime = millis();

  // If the current interval has passed:
  if (currentTime - lastTime >= timerInterval) {
    
    //Serial.println(currentTime); helps in understanding what the heck is actually happening.   
     
    timerCallback();
    lastTime += timerInterval; // Changed from original code-- lastTime = currentTime. This should prevent desync.


    // The below lines essentially alternate timerInterval. 
    // More specifically, the conditional "timerInterval == PMS_READ_DELAY" returns PMS_READ_INTERVAL if true and returns PMS_READ_DELAY if false.
    // First time around, always goes to PMS_READ_DELAY after the start sync offset.
    if (!passedInitialOffset) {
      timerInterval = PMS_READ_DELAY;
      passedInitialOffset = true;
    } else {
      timerInterval = timerInterval == PMS_READ_DELAY ? PMS_READ_INTERVAL : PMS_READ_DELAY;
    }

    
  }

}

// This function, a mini switch statement, decides what we do based on timerInterval 
void timerCallback() {
  if (Serial.available()) {
    processSyncMessage();
  }
  if (timerInterval == PMS_READ_DELAY && passedInitialOffset) {
    readData();

    Serial.print("Data printed and going to sleep. ");
    displayTime();

    pms.sleep();
  } else {

    Serial.print("Waking up. ");
    displayTime();

    pms.wakeUp();
  }
}

// readData() ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void readData()
{
  PMS::DATA data;

  // Clear buffer (removes potentially old data) before read. Some data could have been also sent before switching to passive mode.
  while (Serial.available()) { Serial.read(); }

  Serial.println("Send read request...");
  pms.requestRead();

  Serial.println("Reading data...");
  if (pms.readUntil(data))
  {
    Serial.println();

    Serial.print("PM 1.0 (ug/m3): ");
    Serial.println(data.PM_AE_UG_1_0);

    Serial.print("PM 2.5 (ug/m3): ");
    Serial.println(data.PM_AE_UG_2_5);

    Serial.print("PM 10.0 (ug/m3): ");
    Serial.println(data.PM_AE_UG_10_0);

    Serial.println();
  }
  else
  {
    Serial.println("No data.");
  }

}
// Time Communication Functions ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// RECEIVES THE PROCESSING MESSAGE - SEAN
void processSyncMessage() {
  unsigned long pctime;
  const unsigned long DEFAULT_TIME = 1357041600;  // Jan 1 2013

  if (Serial.find(TIME_HEADER)) {
    pctime = Serial.parseInt();
    if (pctime >= DEFAULT_TIME) {  // check the integer is a valid time (greater than Jan 1 2013); SANITY CHECK - SEAN
      setTime(pctime);             // Sync Arduino clock to the time received on the serial port; THIS IS THE REAL SYNCING LINE - SEAN
    }
  }
}

// SENDS TIME REQUEST - SEAN
time_t requestSync()  //BY DEFAULT, THIS OCCURS EVERY 5 MINUTES - SEAN
{
  Serial.write(TIME_REQUEST);
  return 0;  // the time will be sent later in response to serial mesg
}

// Time Utility Functions ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void displayTime() {
  // Digital clock display of the time, all on the same line. Only displays if time is available, sends message if unavailable.
  if (timeStatus() == timeSet) {
    Serial.print("Time: ");
    Serial.print(hour());
    printDigits(minute());
    printDigits(second());
    Serial.print(" ");
    Serial.print(month());
    Serial.print("/");
    Serial.print(day());
    Serial.print("/");
    Serial.print(year());
    Serial.println();
  } else {
    Serial.println("No time available.");
  }
}


void printDigits(int digits) {
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}
