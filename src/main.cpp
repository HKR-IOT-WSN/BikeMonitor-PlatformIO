#include <Arduino.h>
#include <Wire.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <math.h>

#define WIFI

bool startup_message_sent = false;

// Wifi details
const char* ssid = "ben";
const char* password = "bbbbbbbb";

// HiveMQ details
const char* mqtt_server = "5354c59752f44a4ea5c3bb486a43ac0e.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "IoTWSNBikeMonitor";
const char* mqtt_pass = "11111111aA";

WiFiClientSecure espClient;
PubSubClient client(espClient);

// Timer 0 & 1
hw_timer_t *myTimer = NULL;
hw_timer_t *pulseTimer = NULL;

volatile int hallCount = 0;
volatile bool mqtt_send_hallCount = false;
volatile bool mqtt_send_pulse = false;
volatile float angularSpeed = 0;  //angular speed = regular speed / wheel diameter

// Hall sensor
int hallPin = 16;
int hallVal = 0;

MAX30105 pulseSensor;
const uint8_t BEAT_TIMES_SIZE = 10; //Increase this for more averaging. 4 is good.
unsigned long beatTimes[BEAT_TIMES_SIZE]; //Circular array of beat times
uint8_t oldestBeatTimePos = 0; //index of oldest beat time
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute = 0;
float beatAvg = 0;
uint32_t pulseSensorIr = 0;
const int fCutoff = 1000;
const int fSample = 1 / (411 * 0.000001 * 32);  //Each sensor sample is the average of 32 raw samples, each of which takes 411µs to create


void setup_wifi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wifi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".Status:");
    Serial.println(WiFi.status());
  }

  Serial.println("\nWiFi connected.");

  espClient.setInsecure();        //??? Tell esp32 to trust the HiveMQ certificate
}

void setup_pulseSensor() {
  if (!pulseSensor.begin(Wire, I2C_SPEED_FAST)) { //Use default I2C port, 400kHz speed
    Serial.println("MAX30105 was not found. Please check wiring/power. ");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  pulseSensor.setup(60, 32, 2, 1600, 411, 4096); //Configure sensor with 12mA LED current, 32-sample averaging, enable IR LED, 400Hz sample rate, 411µs pulse width (18 bit res.), 4096pA ADC range
  pulseSensor.setPulseAmplitudeRed(0);  //Turn off red LED (this can't be done through setup())
}


void mqtt_reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    String clientId = "ESP32Client - peiben";

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.print("Connected!");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println("Try again in 2 seconds");
      delay(2000);
    }
  }
}


void IRAM_ATTR onTimer() {
  mqtt_send_hallCount = true;
}

void IRAM_ATTR onPulseTimer() {
  mqtt_send_pulse = true;
}

void ARDUINO_ISR_ATTR isr() {
  ++hallCount;
  static long lastHallPulse = 0;
  angularSpeed = PI * 1000.0 / (millis() - lastHallPulse);
  lastHallPulse = millis();
}

void calcBPM() {
  static bool increasing = false;
  static const float LPF_Beta = (1.0 / fSample) / ((1.0 / fSample) + (1.0 / (2*PI*fCutoff)));
  pulseSensorIr = pulseSensorIr - (LPF_Beta * (pulseSensorIr - pulseSensor.getIR()));  //low-pass filter the infrared signal to remove noise
  static uint32_t irPrev = pulseSensorIr;
  static unsigned long lastBeat = millis();
  
  // Detect change in direction of the IR waveform. There are two such changes per beat.
  // Each maximum (slopes changes from up to down) counts as a beat, and the time difference
  // between two adjacent maxima is used to calculate the BPM.
  if (increasing) {
    // maximum detected, count as beat
    if (pulseSensorIr < irPrev) {
      increasing = false;
      Serial.println(">DIRECTION:0");

      const unsigned long now = millis();
      beatsPerMinute = 60000.0 / (now - lastBeat);
      lastBeat = now;

      beatAvg = 60000.0 / ( (float) (now - beatTimes[oldestBeatTimePos]) / BEAT_TIMES_SIZE);  //calculate avg bpm from average time between the last BEAT_TIMES_SIZE beats
      beatTimes[oldestBeatTimePos++] = now; //overwrite oldest beat time
      oldestBeatTimePos %= BEAT_TIMES_SIZE; //Wrap variable
    }
  }
  else {
    if (pulseSensorIr > irPrev) {
      increasing = true;
      Serial.println(">DIRECTION:1");
    }
  }

  irPrev = pulseSensorIr;
}

/*
void calcBPMOld() {
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
 */

void setup() {
  Serial.begin(921600);
  Serial.println("Test");
  pinMode(hallPin, INPUT);
  attachInterrupt(hallPin, isr, FALLING);

  #ifdef WIFI
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  #endif

  // HallCount timer setup
  myTimer = timerBegin(0, 80, true);    // Timer 0: 80MHz / 80 = 1MHz (1 microsecond per tick)
  timerAttachInterrupt(myTimer, &onTimer, true);  // Egde trigger->true
  timerAlarmWrite(myTimer, 4 * 1000000, true);
  timerAlarmEnable(myTimer);

  // Pulse timer setup
  pulseTimer = timerBegin(1, 80, true); // Timer 1
  timerAttachInterrupt(pulseTimer, &onPulseTimer, true);
  timerAlarmWrite(pulseTimer, 0.125 * 1000000, true);
  timerAlarmEnable(pulseTimer);

  setup_pulseSensor();

}

void loop() {

  #ifdef WIFI
  if (!client.connected()) {
    mqtt_reconnect();
  }

  if (!startup_message_sent) {
    client.publish("data/debug", "STARTED UP...");
    startup_message_sent = true;
  }

  if (mqtt_send_hallCount) {
    mqtt_send_hallCount = false;
    client.publish("data/hallCount", String(hallCount).c_str());
    client.publish("data/angularSpeed", String(angularSpeed, 3).c_str());
    hallCount = 0;
  }

  if (mqtt_send_pulse) {
    client.publish("data/bpm", String(beatsPerMinute, 0).c_str());
    client.publish("data/bpmAvg", String(beatAvg, 0).c_str());
  }
  #endif

  calcBPM();
  Serial.print(">BPM:");
  Serial.println(beatsPerMinute);
  Serial.print(">Avg BPM:");
  Serial.println(beatAvg);
  Serial.print(">IR:");
  Serial.println(pulseSensorIr);
}