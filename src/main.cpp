#include "Arduino.h"
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

// Wifi details
const char* ssid = "Galaxy";
const char* password = "11111111";

// HiveMQ details
const char* mqtt_server = "5354c59752f44a4ea5c3bb486a43ac0e.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "IoTWSNBikeMonitor";
const char* mqtt_pass = "11111111aA";

WiFiClientSecure espClient;
PubSubClient client(espClient);

// Timer 0
hw_timer_t *myTimer = NULL;
volatile int hallCounter = 0;
volatile int speed = 0;
volatile bool mqtt_send = false;

// Hall sensor
int hallPin = 15;
int hallVal = 0;
RTC_DATA_ATTR int counter = 0;

MAX30105 pulseSensor;
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute = 0;
int beatAvg = 0;



void setup_wifi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wifi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    Serial.print("Status:");
    Serial.print(WiFi.status());
    Serial.print("\n");
  }

  Serial.println("\nWiFi connected.");

  espClient.setInsecure();        //??? Tell esp32 to trust the HiveMQ certificate
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
  speed = hallCounter;            // edit speed here with circumference
  Serial.print("BikeSpeed:");
  Serial.print(speed);
  Serial.print("\n");
  hallCounter = 0;
  
  mqtt_send = true;
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

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

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
  
  if (!client.connected()) {
    mqtt_reconnect();
  }

  if (mqtt_send) {
    mqtt_send = false;
    String payload = String(speed) + " " + String(++counter) + " "
                  + String(beatsPerMinute) + " " + String(beatAvg) + " "
                  + String(pulseSensor.getIR()); 
    client.publish("data/speed,counter,beats/min,avg,IR", payload.c_str());
  }

  calcBPM();
  // Serial.print(">BPM:");
  // Serial.println(beatsPerMinute);
  // Serial.print(">Avg BPM:");
  // Serial.println(beatAvg);
  // Serial.print(">IR:");
  // Serial.println(pulseSensor.getIR());
}