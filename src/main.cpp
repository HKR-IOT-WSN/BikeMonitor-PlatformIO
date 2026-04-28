#include "Arduino.h"
#include <Wire.h>
#include "MAX30105.h"

#include "heartRate.h"


hw_timer_t *myTimer = NULL;
volatile int hallCounter = 0;
volatile int speed = 0;   //cm per second

int hallPin = 15;
int hallVal = 0;

MAX30105 pulseSensor;
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute = 0;
int beatAvg = 0;

void IRAM_ATTR onTimer() {
  speed = hallCounter;
  Serial.print("BikeSpeed:");
  Serial.print(speed);
  Serial.print("\n");
  hallCounter = 0;

}

void ARDUINO_ISR_ATTR isr() {
  ++hallCounter;
}

void calcBPM() {
  long irValue = pulseSensor.getIR();

  if (checkForBeat(irValue) == true) {
    //We sensed a beat!
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
      rateSpot %= RATE_SIZE; //Wrap variable

      //Take average of readings
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Test");
  pinMode(hallPin, INPUT);
  attachInterrupt(hallPin, isr, FALLING);

  myTimer = timerBegin(0, 80, true);    // 80MHz / 80 = 1MHz (1 microsecond per tick)
  timerAttachInterrupt(myTimer, &onTimer, true);  // Egde trigger->true
  timerAlarmWrite(myTimer, 10000000, true);
  timerAlarmEnable(myTimer);

   // Pulse sensor
  if (!pulseSensor.begin(Wire, I2C_SPEED_FAST)) { //Use default I2C port, 400kHz speed
    Serial.println("MAX30105 was not found. Please check wiring/power. ");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  pulseSensor.setup(20, 4, 2, 3200, 118, 2048); //Configure sensor with 4mA LED current, 4-sample averaging, both red & IR LED, max sample rate, 118µs pulse width (16 bit res.), min ADC range
}

void loop() {
  
  // if (!digitalRead(hallPin)) {    // if sensing magnet
  //   ++hallCounter;
  // };

  calcBPM();
  Serial.print(">BPM:");
  Serial.println(beatsPerMinute);
  Serial.print(">IR:");
  Serial.println(pulseSensor.getIR());
}