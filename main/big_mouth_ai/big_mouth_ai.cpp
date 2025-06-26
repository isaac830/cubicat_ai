#include "big_mouth_ai.h"
#include <utils/helper.h>
#include "cubicat.h"
#include "system_info.h"
#include <esp_random.h>
#include "esp_chip_info.h"
#include <esp_ota_ops.h>
#include "esp_app_desc.h"
#include <mbedtls/base64.h>
#include "../rpc/msg.pb-c.h"
#include "../proto_socket.h"

#define FG_TASK_EVENT (1 << 0)
#define AUDIO_TASK_EVENT (1 << 1)
#define STOP_SPEAK_EVENT (1 << 2)
#define LANG_CN "zh-CN"
#define BOARD_TYPE "bread-compact-wifi"
#define BOARD_NAME BOARD_TYPE
#define LOCK_OPUS_BUFFER std::lock_guard<std::recursive_mutex> lock(m_opusMutex);


std::string session_id = "";

BigMouthAI::BigMouthAI()
{
    m_eventGroup = xEventGroupCreate();
    m_wakeWordDetect.Initialize(1, false);
#ifdef CONFIG_AUDIO_PROCESSING
    m_audioProcessor.Initialize(2, true);
    m_loopbackBuffer.allocate(1024 * 2);
#endif
    registerRpcHandler();
    m_pMcpServer = new MCPServer();
}

BigMouthAI::~BigMouthAI()
{
#ifdef CONFIG_AUDIO_PROCESSING
    m_loopbackBuffer.release();
#endif
    vEventGroupDelete(m_eventGroup);
    delete m_pMcpServer;
}
void AudioTask(void* param) {
    auto ai = (BigMouthAI*)param;
    while (true)
    {
        ai->audioLoop();
    }
}
void BigMouthAI::onBeginConnect() {

}

void BigMouthAI::onConnected(Socket* socket) {
    m_pSocket = (ProtoSocket*)socket;
    m_pMcpServer->setSocket(m_pSocket);
    if (!m_audioTaskHandle)
        xTaskCreatePinnedToCoreWithCaps(AudioTask, "Audio Task", 1024*32, this, 1, &m_audioTaskHandle, getSubCoreId(), MALLOC_CAP_SPIRAM);
    Rpc__Login login = RPC__LOGIN__INIT;
    login.accounttype = RPC__ACCOUNT_TYPE__Guest;
    login.has_accounttype = 1;
    login.name = (const char*)"isaac";
    m_pSocket->send("login", &login);
}

void BigMouthAI::onDisconnected() {
    setState(Idle);
    foregroundTask([this]() {
        if (m_connectionCallback)
            m_connectionCallback(false);
    });
}

void BigMouthAI::onBinaryData(const char* data, unsigned int len) {
    if (getState() == Speaking) {
        LOCK_OPUS_BUFFER
        m_opusBufferQueue.emplace_back(std::move(std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)));
    }
}

void BigMouthAI::onRequest(Rpc__Request* request) {
    auto it = m_rpcHandlers.find(std::string("Rpc__")+request->protoname);
    if (it != m_rpcHandlers.end()) {
        it->second(request);
    } else if (std::string(request->protoname) != "Ping") {
        LOGE("Missing handler function for message: %s", request->protoname);
    }
}

void BigMouthAI::onJsonData(cJSON* root) {
    auto type = cJSON_GetObjectItem(root, "type");
    if (!type) {
        LOGE("Missing message type, data: %s", root->valuestring);
        return;
    }
    if (strcmp(type->valuestring, "tts") == 0) {
        auto state = cJSON_GetObjectItem(root, "state");
        if (strcmp(state->valuestring, "start") == 0) {
            setState(Speaking);
        } else if (strcmp(state->valuestring, "stop") == 0) {
            // 等待所有缓存语音数据播放完毕才能切换状态
            xEventGroupSetBits(m_eventGroup, STOP_SPEAK_EVENT);
        } else if (strcmp(state->valuestring, "sentence_start") == 0) {
            auto textItem = cJSON_GetObjectItem(root, "text");
            if (textItem != NULL) {
                std::string text = textItem->valuestring;
                LOGI("<< %s", text.c_str());
                foregroundTask([text, this]() {
                    if (m_ttsCallback) {
                        m_ttsCallback(text);
                    } 
                });
            }
        }
    } else if (strcmp(type->valuestring, "stt") == 0) {
        // printf("stt result: %s\n", cJSON_GetObjectItem(root, "text")->valuestring);
    } else if (strcmp(type->valuestring, "llm") == 0) {
        std::string emo = cJSON_GetObjectItem(root, "emotion")->valuestring;
        printf("llm result: %s\n", emo.c_str());
        foregroundTask([emo, this]() {
            if (m_llmCallback) {
                Emotion e;
                if (emo == "neutral") {
                    e = Neutral;
                } else if (emo == "happy") {
                    e = Happy;
                } else if (emo == "sad") {
                    e = Sad;
                } else if (emo == "angry") {
                    e = Angry;
                } else if (emo == "surprise") {
                    e = Surprise;
                } else if (emo == "disgust") {
                    e = Disgust;
                } else if (emo == "fear") {
                    e = Fear;
                } else {
                    e = Unknown;
                }
                m_llmCallback(e);
            }
        });
    } else if (strcmp(type->valuestring, "iot") == 0) {
        auto commands = cJSON_GetObjectItem(root, "commands");
        if (commands != NULL) {
            printf("iot commands:%s\n", commands->valuestring);
        }
    } else if (strcmp(type->valuestring, "mcp") == 0) {
        auto result = m_pMcpServer->eval(root);
        Rpc__Msg msg = RPC__MSG__INIT;
        msg.text = (char*)result.c_str();
        m_pSocket->send("jsonMessage", &msg);
    }
}

void BigMouthAI::onServerHello(const cJSON* root) {
    printf("On server hello!!\n");
    uint16_t sampleRate = CUBICAT.speaker.getSampleRate();
    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (audio_params != NULL) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        auto channels = cJSON_GetObjectItem(audio_params, "channels");
        if (sample_rate != NULL) {
            sampleRate = sample_rate->valueint;
            CUBICAT.speaker.setSampleRate(sampleRate);
            printf("Server audio sample rate: %d channels: %d\n", sampleRate, channels->valueint);
            CUBICAT.speaker.setVolume(1.0f);
#ifdef CONFIG_AUDIO_PROCESSING
            if (sampleRate != 16000) {
                m_speakerResampler.Configure(sampleRate, 16000);
            }
#endif
        }
    }
    if (!m_pOpusDecoder)
        m_pOpusDecoder = std::make_unique<OpusDecoderWrapper>(sampleRate, 1, OPUS_FRAME_DURATION_MS);
    if (!m_pOpusEncoder) {
        m_pOpusEncoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
        m_pOpusEncoder->SetComplexity(3);
        CUBICAT.mic.setSampleRate(16000);
    }
    m_opusFrameSize = 16000 / 1000 * 1 * OPUS_FRAME_DURATION_MS;
    m_wakeWordDetect.OnWakeWordDetected([this](const std::string& wake_word) {
        m_sLastWakeWord = wake_word;
        foregroundTask([this, &wake_word]() {
            if (m_eDeviceState == Speaking) {
                abortSpeaking();
            }
            if (m_eDeviceState == Idle) {
                setState(Connecting);
                m_wakeWordDetect.EncodeWakeWordData();
                // Reopen audio channel if audio channel is closed
                if (!m_pSocket->isConnected()) {
                    m_bAutoWakeOnReconnect = true;
                    m_pSocket->reconnect();
                    return;
                }
                onWakeWord();
            }
        });
    });
    m_wakeWordDetect.StartDetection();
    CUBICAT.mic.start();
    CUBICAT.speaker.setEnable(true);
    if (m_bAutoWakeOnReconnect) {
        onWakeWord();
        m_bAutoWakeOnReconnect = false;   
    }
    // Audio processing 
#ifdef CONFIG_AUDIO_PROCESSING
    m_audioProcessor.OnOutput([this](std::vector<int16_t>&& data) {
        audioTask([this, data = std::move(data)]() mutable {
            m_pOpusEncoder->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                sendAudio(opus.data(), opus.size());
            });  
        });
    });
    m_audioProcessor.Stop();
#endif
    foregroundTask([this]() {
        if (m_connectionCallback) {
            m_connectionCallback(true);
        }
    });
}

void BigMouthAI::onWakeWord() {
    std::vector<uint8_t> opus;
    // Encode and send the wake word data to the server
    while (m_wakeWordDetect.GetWakeWordOpus(opus)) {
        sendAudio(opus.data(), opus.size());
    }
    // Set the chat state to wake word detected
    sendWakeWord(m_sLastWakeWord);

    setState(Idle);
}

void BigMouthAI::loop() {
    auto bits = xEventGroupWaitBits(m_eventGroup, FG_TASK_EVENT | STOP_SPEAK_EVENT, pdFALSE, pdFALSE, 0);
    if (bits & FG_TASK_EVENT) {
        std::lock_guard<std::recursive_mutex> lock(m_taskMutex);
        while (!m_bgTasks.empty())
        {
            auto& func = m_bgTasks.front();
            func();
            m_bgTasks.pop_front();
        }
        xEventGroupClearBits(m_eventGroup, FG_TASK_EVENT);
    }
    if (bits & STOP_SPEAK_EVENT) {
        if (m_eDeviceState == Speaking) {
            if (m_opusBufferQueue.empty()) {
                setState(Listening);
                xEventGroupClearBits(m_eventGroup, STOP_SPEAK_EVENT);
            }
        } else {
            xEventGroupClearBits(m_eventGroup, STOP_SPEAK_EVENT);
        }
    }
    auto now = timeNow();
    if (m_pSocket && now - m_lastPingTime >= 5) {
        m_pSocket->ping();
        m_lastPingTime = now;
    }
    m_pMcpServer->loop();
}

void BigMouthAI::audioLoop() {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    auto bits = xEventGroupWaitBits(m_eventGroup, AUDIO_TASK_EVENT, pdTRUE, pdFALSE, 0);
    if (bits & AUDIO_TASK_EVENT) {
        std::lock_guard<std::recursive_mutex> lock(m_audioTaskMutex);
        while (!m_audioTasks.empty())
        {
            auto& func = m_audioTasks.front();
            func();
            m_audioTasks.pop_front();
        }
    }
    std::vector<int16_t> micPCM = CUBICAT.mic.popAudioBuffer(0);
    // Wake word detection data feed
    if (micPCM.size()) {
        if (m_wakeWordDetect.IsDetectionRunning()) {
            m_wakeWordDetect.Feed(micPCM);
        }
    }
    if (getState() == Speaking) {
        if (m_opusBufferQueue.empty()) {
            return;
        }
        std::unique_lock<std::recursive_mutex> lock(m_opusMutex);
        auto opus = std::move(m_opusBufferQueue.front());
        m_opusBufferQueue.pop_front();
        lock.unlock();
        // decode opus
        std::vector<int16_t> playPCM;
        if (!m_pOpusDecoder->Decode(std::move(opus), playPCM)) {
            return;
        }
#ifdef CONFIG_AUDIO_PROCESSING
        // Input audio aec process
        std::vector<int16_t>* resampledRef = &playPCM;
        std::vector<int16_t> resampleBuff;
        if (m_audioProcessor.IsRunning()) {
            // combine pcm with output pcm which is work as reference
            std::vector<int16_t> pcmWithRef;
            // resample speaker pcm data if speaker sample rate is not 16k
            if (CUBICAT.speaker.getSampleRate() != CUBICAT.mic.getSampleRate()) {
                resampleBuff.resize(m_speakerResampler.GetOutputSamples(playPCM.size()));
                m_speakerResampler.Process(playPCM.data(), playPCM.size(), resampleBuff.data());
                resampledRef = &resampleBuff;
            }
            if (m_loopbackBuffer.len >= micPCM.size() * 2) {
                pcmWithRef.resize(micPCM.size() * 2);
                int16_t* loopback = (int16_t*)m_loopbackBuffer.data + (m_loopbackBuffer.len/2 - micPCM.size());
                for (int i = 0; i < micPCM.size(); i++) {
                    pcmWithRef[i*2] = micPCM[i];
                    pcmWithRef[i*2 + 1] = *(loopback++);
                }
                m_audioProcessor.Feed(pcmWithRef);   
            }
        }
#endif
        CUBICAT.speaker.playRaw(playPCM.data(), playPCM.size(), 1);
#ifdef CONFIG_AUDIO_PROCESSING
        m_loopbackBuffer.append((uint8_t*)resampledRef->data(), resampledRef->size() * sizeof(int16_t));
#endif
    } else if (getState() == Listening) {
        if (micPCM.size()) {
            m_pOpusEncoder->Encode(std::move(micPCM),
             [this](std::vector<uint8_t>&& opus) {
                sendAudio(opus.data(), opus.size());
            });
        }
    }
}


void BigMouthAI::foregroundTask(std::function<void()> callback) {
    std::lock_guard<std::recursive_mutex> lock(m_taskMutex);
    m_bgTasks.push_back(callback);
    xEventGroupSetBits(m_eventGroup, FG_TASK_EVENT);
}

void BigMouthAI::audioTask(std::function<void()> callback) {
    std::lock_guard<std::recursive_mutex> lock(m_audioTaskMutex);
    m_audioTasks.push_back(callback);
    xEventGroupSetBits(m_eventGroup, AUDIO_TASK_EVENT);
}

void BigMouthAI::abortSpeaking() {
    setState(Idle);
    std::string message = "{\"session_id\":\"" + session_id + "\",\"type\":\"abort\"";
    if (true) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    Rpc__Msg msg = RPC__MSG__INIT;
    msg.text = (char*)message.c_str();
    m_pSocket->send("jsonMessage", &msg);
}
void BigMouthAI::sendWakeWord(const std::string& wakeWord) {
    std::string json = "{\"session_id\":\"" + session_id + 
    "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + wakeWord + "\"}";
    Rpc__Msg msg = RPC__MSG__INIT;
    msg.text = (char*)json.c_str();
    m_pSocket->send("jsonMessage", &msg);
}

void BigMouthAI::sendAudio(const uint8_t* data, size_t len) {
    assert(data != nullptr && len > 0);
    Rpc__BytesMsg msg = RPC__BYTES_MSG__INIT;
    msg.data = {len, (uint8_t*)data};
    m_pSocket->send("audioMessage", &msg);
}

void BigMouthAI::sendStartListening(ListeningMode mode) {
    std::string message = "{\"session_id\":\"" + session_id + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";
    if (mode == Realtime) {
        message += ",\"mode\":\"realtime\"";
    } else if (mode == AutoStop) {
        message += ",\"mode\":\"auto\"";
    } else {
        message += ",\"mode\":\"manual\"";
    }
    message += "}";
    Rpc__Msg msg = RPC__MSG__INIT;
    msg.text = (char*)message.c_str();
    m_pSocket->send("jsonMessage", &msg);
}

std::string GenerateUuid() {
    // UUID v4 需要 16 字节的随机数据
    uint8_t uuid[16];
    
    // 使用 ESP32 的硬件随机数生成器
    esp_fill_random(uuid, sizeof(uuid));
    
    // 设置版本 (版本 4) 和变体位
    uuid[6] = (uuid[6] & 0x0F) | 0x40;    // 版本 4
    uuid[8] = (uuid[8] & 0x3F) | 0x80;    // 变体 1
    
    // 将字节转换为标准的 UUID 字符串格式
    char uuid_str[37];
    snprintf(uuid_str, sizeof(uuid_str),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11],
        uuid[12], uuid[13], uuid[14], uuid[15]);
    
    return std::string(uuid_str);
}
std::string BigMouthAI::getUUID() {
    auto uuid = CUBICAT.storage.getString("uuid");
    if (uuid.empty()) {
        uuid = GenerateUuid();
        CUBICAT.storage.setString("uuid", uuid.c_str());
    }
    return uuid;
}
void BigMouthAI::reboot() {
    LOGI("Rebooting...");
    esp_restart();
}

void BigMouthAI::setState(DeviceState state) {
    if (m_eDeviceState != state) {
        m_eDeviceState = state;
        onStateChange();
    }
}

void BigMouthAI::onStateChange() {
    printf("state change: %s\n", getCurrentStateName().c_str());
    if (getState() == Idle) {
        CUBICAT.speaker.setEnable(false);
        m_wakeWordDetect.StartDetection();
#ifdef CONFIG_AUDIO_PROCESSING
        m_audioProcessor.Stop();
#endif
    } else if (getState() == Connecting) {

    } else if (getState() == Speaking) {
        LOCK_OPUS_BUFFER
        m_opusBufferQueue.clear();
        CUBICAT.speaker.setEnable(true);
        m_wakeWordDetect.StopDetection();
        m_pOpusDecoder->ResetState();
    } else if (getState() == Listening) {
        CUBICAT.speaker.setEnable(false);
        CUBICAT.mic.start();
#ifdef CONFIG_AUDIO_PROCESSING
        if (!m_audioProcessor.IsRunning()) {
            sendStartListening(Realtime);
            m_audioProcessor.Start();
        }
#else
        sendStartListening(AutoStop);
#endif
        audioTask([this](){
            m_pOpusEncoder->ResetState();
        });
        m_wakeWordDetect.StopDetection();
    }
    auto state = getState();
    foregroundTask([this, state]() {
        if (m_stateCallback) {
            m_stateCallback(state);
        }
    });
}

std::string BigMouthAI::getCurrentStateName() {
    switch (m_eDeviceState) {
        case DeviceState::Idle:
            return "idle";
        case DeviceState::Connecting:
            return "connecting";
        case DeviceState::Speaking:
            return "speaking";
        case DeviceState::Listening:
            return "listening";
        case DeviceState::Upgrading:
            return "upgrading";
        default:
            return "unknown";
    }
}
