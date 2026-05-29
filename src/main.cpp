#include <Arduino.h>
#include <Wire.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <math.h>

#define WIFI

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

// Shared structs for thread-safe communication
struct HallData {
  int count;
};

struct PulseData {
  float bpm;
  float bpmAvg;
};

// Queue handles
QueueHandle_t hallQueue;
QueueHandle_t pulseQueue;
TaskHandle_t MQTTTaskHandle;

// Timer 0 & 1
hw_timer_t *myTimer = NULL;
hw_timer_t *pulseTimer = NULL;

volatile int hallCount = 0;
int hallPin = 16;


// Pulse sensor config 
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

// --------- CORE 1 ISR ---------
void ARDUINO_ISR_ATTR magnetIsr() {
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();

  // 15ms Software Debounce to prevent electrical noise spikes from WiFi
  if (interrupt_time - last_interrupt_time > 15) {
    ++hallCount;
  }
  
  last_interrupt_time = interrupt_time;
}

void IRAM_ATTR onHallTimer() {
  HallData data;
  data.count = hallCount;
  hallCount = 0;      // reset hallCount

  // Push to core 0 without blocking
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(hallQueue, &data, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void IRAM_ATTR onPulseTimer() {
  PulseData data;
  data.bpm = beatsPerMinute;
  data.bpmAvg = beatAvg;

  // Push to core 0 without blocking
  BaseType_t xHighPriorityTaskWorken = pdFALSE;
  xQueueSendFromISR(pulseQueue, &data, &xHighPriorityTaskWorken);
  if (xHighPriorityTaskWorken) portYIELD_FROM_ISR();
}

// --------- CORE 1: Pulse Calculations ---------
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
      //Serial.println(">DIRECTION:0");

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
      //Serial.println(">DIRECTION:1");
    }
  }

  irPrev = pulseSensorIr;
}

// --------- CORE 0: MQTT & Networking worker ---------
void mqttWorkerTask(void * pvParameters) {
  #ifdef WIFI
  WiFi.begin(ssid, password);
  // Serial.print("Connecting to Wifi");

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    // Serial.print(".Status:");
    // Serial.println(WiFi.status());
  }
  
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  
  bool startup_message_sent = false;
  HallData rxHall;
  PulseData rxPulse;

  for(;;) {
    if (!client.connected()) {
      String clientId = "ESP32Client-peiben";
      if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
        if (!startup_message_sent) {
          client.publish("data/debug", "STARTED UP...");
          startup_message_sent = true;
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
    }

    client.loop(); // Keep background TLS connection alive

    // Read and parse incoming Hall sensor metrics from Core 1
    // if there is no task, wait 0 ticks, and move on to the next line
    if (xQueueReceive(hallQueue, &rxHall, 0) == pdTRUE) {
      client.publish("data/hallCount", String(rxHall.count).c_str());
    }

    // Read and parse incoming Pulse readings from Core 1
    if (xQueueReceive(pulseQueue, &rxPulse, 0) == pdTRUE) {
      client.publish("data/bpm", String(rxPulse.bpm, 0).c_str());
      client.publish("data/bpmAvg", String(rxPulse.bpmAvg, 0).c_str());
    }
    
    #endif
    vTaskDelay(pdMS_TO_TICKS(5)); // Leave room for core 0 system tasks
  }
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

void setup() {
  Serial.begin(921600);

  // Use INPUT_PULLUP to mitigate RF interference spikes
  pinMode(hallPin, INPUT_PULLUP);
  attachInterrupt(hallPin, magnetIsr, FALLING);

  // Instantiating thread-safe messaging buffers
  hallQueue = xQueueCreate(5, sizeof(HallData));
  pulseQueue = xQueueCreate(5, sizeof(PulseData));

  // Initialize HallCount timer
  myTimer = timerBegin(0, 80, true);    // Timer 0: 80MHz / 80 = 1MHz (1 microsecond per tick)
  timerAttachInterrupt(myTimer, &onHallTimer, true);  // Egde trigger->true
  timerAlarmWrite(myTimer, 4 * 1000000, true);
  timerAlarmEnable(myTimer);

  // Pulse timer setup
  pulseTimer = timerBegin(1, 80, true); // Timer 1
  timerAttachInterrupt(pulseTimer, &onPulseTimer, true);
  timerAlarmWrite(pulseTimer, 0.125 * 1000000, true);
  timerAlarmEnable(pulseTimer);

  setup_pulseSensor();

  // Spawning Network Engine strictly on Core 0
  xTaskCreatePinnedToCore(
    mqttWorkerTask,
    "MQTT_Netowrk_Task",
    10240,  // Expand stack space for deep TLS/SSL packets
    NULL,
    1,
    &MQTTTaskHandle,
    0       // Core 0
  );

}

void loop() {
  // Main Thread automatically executes on Core 1
  calcBPM();

  // Local serial output for debugging (does not impact WiFi stability)

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 250) {
    Serial.print(">BPM:");
    Serial.println(beatsPerMinute);
    Serial.print(">Avg BPM:");
    Serial.println(beatAvg);
    Serial.print(">IR:");
    Serial.println(pulseSensorIr);
    lastPrint = millis();
  }
}