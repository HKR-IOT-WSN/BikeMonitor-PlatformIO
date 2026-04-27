#include "MAX3010x.h"
#include "filters.h"

class BpmSPO2Calc
{
    MAX30102 sensor;
    static constexpr float kSamplingFrequency = 3200.0;

    // Finger Detection Threshold and Cooldown
    static constexpr unsigned long kFingerThreshold = 10000;
    static constexpr unsigned int kFingerCooldownMs = 500;

    // Edge Detection Threshold (decrease for MAX30100)
    static constexpr float kEdgeThreshold = -500.0;

    // Filters
    static constexpr float kLowPassCutoff = 5.0;
    static constexpr float kHighPassCutoff = 0.5;

    // Averaging
    static constexpr bool kEnableAveraging = false;
    static constexpr int kAveragingSamples = 50;
    static constexpr int kSampleThreshold = 5;

    HighPassFilter high_pass_filter{kHighPassCutoff, kSamplingFrequency};
    LowPassFilter low_pass_filter{kLowPassCutoff, kSamplingFrequency};
    Differentiator differentiator{kSamplingFrequency};
    MovingAverageFilter<kAveragingSamples> averager;

    // Timestamp of the last heartbeat
    long last_heartbeat = 0;

    // Timestamp for finger detection
    long finger_timestamp = 0;
    bool finger_detected = false;

    // Last diff to detect zero crossing
    float last_diff = NAN;
    bool crossed = false;
    long crossed_time = 0;

    int bpm = -1;


public:

    bool begin();

    void loop();

    bool bpmAvailable();

    int getBpm();
};