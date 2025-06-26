#ifndef WAKE_WORD_DETECT_AFE_H
#define WAKE_WORD_DETECT_AFE_H
#include "wake_word_detect.h"

class WakeWordDetectAFE : public WakeWordDetect {
public:
    ~WakeWordDetectAFE();

    void Initialize(int channels, bool reference) override;
    void Feed(const std::vector<int16_t>& data) override;

    void StopDetection() override;
private:
    esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;

    void StoreWakeWordData(uint16_t* data, size_t size);
    void AudioDetectionTask();
};

#endif
