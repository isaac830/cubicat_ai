#include "wake_word_detect_afe.h"

#include <esp_log.h>
#include <model_path.h>
#include <arpa/inet.h>
#include <sstream>
#include <esp_timer.h>
#include "opus_encoder.h"

static const char* TAG = "WakeWordDetectAFE";


WakeWordDetectAFE::~WakeWordDetectAFE() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
}

void WakeWordDetectAFE::Initialize(int channels, bool reference) {
    channels_ = channels;
    reference_ = reference;
    int ref_num = reference_ ? 1 : 0;
    const char* modelPartition = "model";
    srmodel_list_t *models = esp_srmodel_init(modelPartition);
    if (!models || models->num <= 0) {
        ESP_LOGE(TAG, "Failed to load models at partition: %s", modelPartition);
        return;
    }
    ESP_LOGI(TAG, "Found %d models", models->num);
    for (int i = 0; i < models->num; i++) {
        ESP_LOGI(TAG, "Model %d: %s", i, models->model_name[i]);
        if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL) {
            wakenet_model_ = models->model_name[i];
            auto words = esp_srmodel_get_wake_words(models, wakenet_model_);
            // split by ";" to get all wake words
            std::stringstream ss(words);
            std::string word;
            while (std::getline(ss, word, ';')) {
                wake_words_.push_back(word);
            }
        }
    }

    std::string input_format;
    for (int i = 0; i < channels_ - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_config->aec_init = reference_;
    afe_config->aec_mode = AEC_MODE_VOIP_LOW_COST;
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config->wakenet_mode = DET_MODE_95;
    
    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);

    xTaskCreateWithCaps([](void* arg) {
        auto this_ = (WakeWordDetectAFE*)arg;
        this_->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "audio_detection", 4096, this, 3, nullptr, MALLOC_CAP_SPIRAM);
}

void WakeWordDetectAFE::StopDetection() {
    WakeWordDetect::StopDetection();
    afe_iface_->reset_buffer(afe_data_);
}

void WakeWordDetectAFE::Feed(const std::vector<int16_t>& data) {
    if (!afe_iface_) {
        ESP_LOGE(TAG, "AFE interface not initialized");
        return;
    }
    input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());

    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_) * channels_;
    while (input_buffer_.size() >= feed_size) {
        afe_iface_->feed(afe_data_, input_buffer_.data());
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + feed_size);
    }
}

void WakeWordDetectAFE::AudioDetectionTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio detection task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    while (true) {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);
        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            continue;
        }
        // Store the wake word data for voice recognition, like who is speaking
        StoreWakeWordData((uint16_t*)res->data, res->data_size / sizeof(uint16_t));
        if (res->wakeup_state == WAKENET_DETECTED) {
            StopDetection();
            last_detected_wake_word_ = wake_words_[res->wake_word_index - 1];

            if (wake_word_detected_callback_) {
                wake_word_detected_callback_(last_detected_wake_word_);
            }
        }
    }
}

void WakeWordDetectAFE::StoreWakeWordData(uint16_t* data, size_t samples) {
    // store audio data to wake_word_pcm_
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    // keep about 2 seconds of data, detect duration is 32ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 32) {
        wake_word_pcm_.pop_front();
    }
}
