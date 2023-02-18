#include "PMS.h"
#include "SoftwareSerial.h"

// BASIC VARIABLE SETUP -----------------------------------------------------------------------------------------------
// Serial is for printing.
// SerialPMS communicates with PMS Sensor.
// Both are 9600 baud.
SoftwareSerial SerialPMS(2, 3);
PMS pms(SerialPMS);

// PMS_READ_INTERVAL (8 sec) and PMS_READ_DELAY (5 sec) define, respectively, the time between sleep and wake, and the time between wake and read.
// THEY CAN'T BE EQUAL, because their lengths are used to detect sensor state.
static const uint32_t PMS_READ_INTERVAL = 40000;
static const uint32_t PMS_READ_DELAY = 20000;

// This variable, timerInterval, is the crux of the code. 
// It alternates between the values of PMS_READ_INTERVAL and PMS_READ_DELAY to determine the state of the code. 
// We start in the state between waking and reading: PMS_READ_DELAY.
uint32_t timerInterval = PMS_READ_DELAY; 

// setup() -------------------------------------------------------------------------------------------------------------
void setup()
{
  SerialPMS.begin(9600); 
  Serial.begin(9600);

  // Switch to passive mode.
  pms.passiveMode();

  // Default state after sensor power, but undefined after ESP restart e.g. by OTA flash, so we have to manually wake up the sensor for sure.
  // Some logs from bootloader is sent via Serial port to the sensor after power up. This can cause invalid first read or wake up so be patient and wait for next read cycle.
  // Sean -- I don't think these two comments are relevant because the demo code was using a different board called ESP, but I kept them in case
  pms.wakeUp();

  // To start us off, you'd think we'd need the below three lines to delay, read, and sleep.
  // But, because of the way loop() is designed, we actually don't. Explanation at the bottom of loop()
  //delay(PMS_READ_DELAY);
  //readData();
  //pms.sleep();
}

// loop() and callback() -----------------------------------------------------------------------------------------------
void loop()
{
  static uint32_t lastTime = 0;

  uint32_t currentTime = millis();
  //Serial.println(currentTime); helps in understanding what the heck is actually happening.

  if (currentTime - lastTime >= timerInterval) {
    lastTime = currentTime;
    timerCallback();
    timerInterval = timerInterval == PMS_READ_DELAY ? PMS_READ_INTERVAL : PMS_READ_DELAY;
    // The above line essentially alternates timerInterval. 
    // More specifically, the conditional "timerInterval == PMS_READ_DELAY" returns PMS_READ_INTERVAL if true and returns PMS_READ_DELAY if false.
  }

  // Explanation for how we delay/read/sleep off the get-go:
  // Remember that we start timerInterval in wake-read state. 
  // So, the first time through loop(), the if statement is waiting for PMS_READ_DELAY ms to pass. That gives us our initial delay.
  // We will then get the initial reading and sleeping from within timerCallback().

}

// This function decides what we do based on timerInterval 
void timerCallback() {
  if (timerInterval == PMS_READ_DELAY)
  {
    readData();
    Serial.println("Going to sleep.");
    pms.sleep();
  }
  else 
  {
    Serial.println("Waking up.");
    pms.wakeUp();
  }
}

// readData() -----------------------------------------------------------------------------------------------
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