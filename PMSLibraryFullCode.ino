#include "PMS.h"
#include "SoftwareSerial.h"
#include <TimeLib.h>


// BASIC VARIABLE SETUP ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Serial is for printing.
// SerialPMS communicates with PMS Sensor.
// Both are 9600 baud.
SoftwareSerial SerialPMS(2, 3);
PMS pms(SerialPMS);

// readDelay, originally PMS_READ_DELAY, defines the time between sleep and wake. PMS_READ_INTERVAL was removed, as each read interval is now recalculated in real time in the loop using getNextDelay()--half minute or hour.
// Passed initial is a boolean flag, determining the initial time gap to sync on the dot with the hour.
static const uint32_t readDelay = 10000;

static bool passedInitialDelay = false;
static unsigned long lastTime = 0;

// This variable, timerInterval, is the crux of the code. 
// It alternates between the values of getNextDelay() and readDelay to determine the state of the code, except for at the start, where it becomes initialReadDelay. 
uint32_t timerInterval = readDelay; 

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

  -initialDelay represents the time between the current time, at this point after having setup, woken, etc., and the first second of the next minute.
  -timerInterval is set to initialDelay to make sure that the very first iteration of loop() delays for that long.
     -After the very first iteration, timerInterval is changed to readDelay, signifying wake-read delay, and after the first iteration, business continues as usual in the else statement.
       -The quality of timerInterval being equal to initialDelay is very specifically detected with a boolean flag:
       -This circumvents the direct jump to PMS_INTERVAL_DELAY which would occur if readDelay and initialDelay were coincidentally equal.
  -lastTime is set (initially) to the current time so as to provide the most accurate interval of time since initialDelay.
  -!!!IMPORTANT. It is vital that this code is at the END of setup because it gives the latest possible lastTime, in other words, is closest to loop(). This gives it the most time accuracy.
     -Even putting this code before passiveMode() and wakeUp() gives offset error!
  */

  Serial.print("Initial ");
  displayTime();
  unsigned long initialDelay = getNextHalfMinuteDelay();
  Serial.println("Initial delay to sync (ms): " + String(initialDelay));
  timerInterval = initialDelay;
  lastTime = millis();

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
    
    requestSync();
    timerCallback();
    lastTime += timerInterval; // Changed from original code-- lastTime = currentTime. This should prevent desync.


    // The below lines essentially alternate timerInterval. 
    // More specifically, the conditional "timerInterval == readDelay" returns nextDelay() if true and returns readDelay if false.
    // First time around, timeInterval is always set to readDelay after initialDelay.
    if (!passedInitialDelay) {
      timerInterval = readDelay;
      passedInitialDelay = true;
    } else {
      timerInterval = timerInterval == readDelay ? getNextHalfMinuteDelay() : readDelay;
      Serial.println("Timer interval: " + String(timerInterval));
    }

    
  }

}

// This function, a mini switch statement, decides what we do based on timerInterval 
void timerCallback() {
  if (Serial.available()) {
    processSyncMessage();
  }
  if (timerInterval == readDelay && passedInitialDelay) {
    readData();

    Serial.println("Data printed and going to sleep.");

    pms.sleep();
  } else {

    Serial.print("Waking up. "); displayTime();

    pms.wakeUp();
  }
}
// getDelays() ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
unsigned long getNextHourDelay() {
  unsigned long msToNextMinute = (60 - (unsigned long)second()) * 1000;
  unsigned long msToNext58Min = 0;

  //58: one less minute from current seconds, another less minute from 1 min buffer
  if (minute() == 59) {
    msToNext58Min =  58 * 60000;
  } else {
    msToNext58Min = ((58 - (unsigned long)minute()) * 60000);
  }

  return msToNextMinute + msToNext58Min;
}

// Debugging purposes: readDelay at 10 sec, time in between at 20 sec. 30 sec total loop
unsigned long getNextHalfMinuteDelay() {

  if (second() < 20) {
    return (20 - (unsigned long)second()) * 1000;
  } else {
    return (50 - (unsigned long)second()) * 1000;
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

  Serial.print("Reading data...");
  displayTime();
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
