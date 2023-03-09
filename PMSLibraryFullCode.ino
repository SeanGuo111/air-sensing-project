#include "PMS.h"
#include "SoftwareSerial.h"
#include <TimeLib.h>


// BASIC VARIABLE SETUP ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Serial is for printing.
// SerialPMS communicates with PMS Sensor.
// Both are 9600 baud.
SoftwareSerial SerialPMS(2, 3);
PMS pms(SerialPMS);

static bool justWoke = false;
static bool justRead = false;

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

  //Initial time delay until 59 minute, 0 sec.
  Serial.print("Initial ");
  displayTime();
  unsigned long initialDelay = getInitialDelay();
  Serial.println("Initial delay to sync (ms): " + String(initialDelay));
  // Why delay? Well, I figured a quick delay in the setup, while nothing else is going on, would be harmless, and far preferable to a once-off if statement corresponding to a millis() checking in loop().
  //  Besides, maybe blocking is good-- I don't want any premature readings.
  delay(initialDelay);

}

// loop() and callback() ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void loop()
{  
  // Time Stuff
  if (Serial.available()) {
    processSyncMessage();
  }

  int currentMinute = minute();
  int currentSecond = second();

  if ((currentMinute == 59 && currentSecond == 0) && !justWoke) {
    Serial.print("Waking up. "); displayTime();
    pms.wakeUp();
    
    justRead = false;
    justWoke = true;
    requestSync();

  } else if ((currentMinute == 0 && currentSecond == 0) && !justRead) {
    readData();
    Serial.println("Data printed and going to sleep.");
    pms.sleep();

    justWoke = false;
    justRead = true;
    requestSync();
  }

}

unsigned long getInitialDelay() {
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
