#include "BpmSPO2Calc.h"

bool BpmSPO2Calc::begin() {
    return sensor.begin() 
        && sensor.setSamplingRate(MAX30102::SAMPLING_RATE_3200SPS)
        && sensor.setMode(MAX30102::MODE_SPO2)                          /* Both red and IR LED */
        && sensor.setADCRange(MAX30102::ADC_RANGE_16384NA)              /* Highest ADC range */
        && sensor.setResolution(MAX30102::RESOLUTION_16BIT_118US)       /* Lowest resolution - fastest sampling rate */
        && sensor.setLedCurrent(MAX30102::LED_IR, 255)                  /* Max current for red LED */
        && sensor.setLedCurrent(MAX30102::LED_RED, 255);                /* Max current for IR LED */
}


void BpmSPO2Calc::loop() {
    auto sample = sensor.readSample(1000);
    float current_value = sample.ir;
    Serial.print(">red:");
    Serial.println(sample.red);
    Serial.print(">ir:");
    Serial.println(sample.ir);

    // Detect Finger using raw sensor value
    if(sample.ir > kFingerThreshold) {
    if(millis() - finger_timestamp > kFingerCooldownMs) {
        finger_detected = true;
    }
    }
    else {
    // Reset values if the finger is removed
    differentiator.reset();
    averager.reset();
    low_pass_filter.reset();
    high_pass_filter.reset();

    finger_detected = false;
    finger_timestamp = millis();
    }

    if(finger_detected) {
    current_value = low_pass_filter.process(current_value);
    current_value = high_pass_filter.process(current_value);
    float current_diff = differentiator.process(current_value);

    // Valid values?
    if(!isnan(current_diff) && !isnan(last_diff)) {
        
        // Detect Heartbeat - Zero-Crossing
        if(last_diff > 0 && current_diff < 0) {
        crossed = true;
        crossed_time = millis();
        }
        
        if(current_diff > 0) {
        crossed = false;
        }

        // Detect Heartbeat - Falling Edge Threshold
        if(crossed && current_diff < kEdgeThreshold) {
        if(last_heartbeat != 0 && crossed_time - last_heartbeat > 300) {
            // Show Results
            int bpm = 60000/(crossed_time - last_heartbeat);
            if(bpm > 50 && bpm < 250) {
            // Average?
            if(kEnableAveraging) {
                int average_bpm = averager.process(bpm);

                // Show if enough samples have been collected
                if(averager.count() > kSampleThreshold) {
                Serial.print(">Heart Rate (avg, bpm): ");
                Serial.println(average_bpm);
                }
            }
            else {
                Serial.print(">Heart Rate (current, bpm): ");
                Serial.println(bpm); 
                this->bpm = bpm; 
            }
            }
            else {
                this->bpm = -1;
            }
        }

        crossed = false;
        last_heartbeat = crossed_time;
        }
    }

    last_diff = current_diff;
    }
}


bool BpmSPO2Calc::bpmAvailable() { return bpm != -1; };

int BpmSPO2Calc::getBpm() { return bpm; };