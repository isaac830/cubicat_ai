#ifndef WAKE_WORD_DETECT_H
#define WAKE_WORD_DETECT_H
#include <string>
#include <vector>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_afe_sr_models.h>
#include <esp_nsn_models.h>

#include <list>
#include <mutex>
#include <condition_variable>

#define OPUS_FRAME_DURATION_MS 60
#define DETECTION_RUNNING_EVENT 1

class WakeWordDetect {
public:
    WakeWordDetect();
    virtual ~WakeWordDetect();
    virtual void Initialize(int channels, bool reference) = 0;
    virtual void Feed(const std::vector<int16_t>& data) = 0;

    virtual void StartDetection();
    virtual void StopDetection();
    bool IsDetectionRunning();
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) ;
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }
    void EncodeWakeWordData();
protected:
    char* wakenet_model_ = NULL;
    std::vector<std::string> wake_words_;
    std::vector<int16_t> input_buffer_;
    EventGroupHandle_t event_group_;
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    int channels_;
    bool reference_;
    std::string last_detected_wake_word_;

    TaskHandle_t wake_word_encode_task_ = nullptr;
    StaticTask_t wake_word_encode_task_buffer_;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    std::list<std::vector<int16_t>> wake_word_pcm_;
    std::list<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;

};

#endif
