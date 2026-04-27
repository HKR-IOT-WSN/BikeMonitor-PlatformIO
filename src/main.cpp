#include "Arduino.h"
#include "BluetoothSerial.h"



hw_timer_t *myTimer = NULL;
volatile int hallCounter = 0;
volatile int speed = 0;   //cm per second

int hallPin = 15;
int hallVal = 0;

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

void setup() {
  Serial.begin(115200);
  pinMode(hallPin, INPUT);
  attachInterrupt(hallPin, isr, FALLING);

  myTimer = timerBegin(0, 80, true);    // 80MHa / 80 = 1MHz (1 microsecond per tick)
  timerAttachInterrupt(myTimer, &onTimer, true);  // Egde trigger->true
  timerAlarmWrite(myTimer, 10000000, true);
  timerAlarmEnable(myTimer);


}

void loop() {
  
  // if (!digitalRead(hallPin)) {    // if sensing magnet
  //   ++hallCounter;
  // };




}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}