#define MA_IMPLEMENTATION
#include "miniaudio.h"
#define LOG_TAG "AUDIO"
#include "SimpleLog.h"
#include "AudioCapture.h"
#include <iostream>
#include <algorithm>

namespace Netease {

    AudioCapture::AudioCapture() {
        // deque 不需要 reserve
    }

    AudioCapture::~AudioCapture() {
        Stop();
    }

    void AudioCapture::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, unsigned int frameCount) {
        auto* pCapture = (AudioCapture*)pDevice->pUserData;
        if (!pCapture || !pInput) return;

        pCapture->OnDataInternal((const float*)pInput, frameCount);
    }



    void AudioCapture::OnDataInternal(const float* pInput, unsigned int frameCount) {
        if (!pInput) return;

        int channels = 2;

        std::lock_guard<std::mutex> lock(m_BufferMutex);
        
        // 我们只取单声道混合数据
        for (unsigned int i = 0; i < frameCount; i++) {
            float sample = 0;
            for (int c = 0; c < channels; c++) {
                sample += pInput[i * channels + c];
            }
            sample /= channels;

            m_Buffer.push_back(sample);
            while (m_Buffer.size() > BUFFER_SIZE) {
                m_Buffer.pop_front();  // O(1) 删除
            }
        }
    }

    bool AudioCapture::Start() {
        if (m_IsRunning) return true;

        ma_device_config config = ma_device_config_init(ma_device_type_loopback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 48000;
        config.dataCallback = DataCallback;
        config.pUserData = this;

        m_pDevice = (ma_device*)malloc(sizeof(ma_device));
        if (ma_device_init(NULL, &config, m_pDevice) != MA_SUCCESS) {
            LOG_ERROR("初始化音频回放捕获失败.");
            free(m_pDevice);
            m_pDevice = nullptr;
            return false;
        }

        if (ma_device_start(m_pDevice) != MA_SUCCESS) {
            LOG_ERROR("开始音频回放捕获失败.");
            ma_device_uninit(m_pDevice);
            free(m_pDevice);
            m_pDevice = nullptr;
            return false;
        }

        m_IsRunning = true;
        LOG_INFO("音频回放捕获开始 (WASAPI).");
        return true;
    }

    void AudioCapture::Stop() {
        if (!m_IsRunning) return;

        if (m_pDevice) {
            ma_device_stop(m_pDevice);
            ma_device_uninit(m_pDevice);
            free(m_pDevice);
            m_pDevice = nullptr;
        }
        m_IsRunning = false;
    }

    std::vector<float> AudioCapture::GetSamples(size_t count) {
        std::vector<float> samples;
        std::lock_guard<std::mutex> lock(m_BufferMutex);
        
        size_t available = m_Buffer.size();
        size_t toCopy = (std::min)(count, available);
        
        if (toCopy > 0) {
            samples.assign(m_Buffer.end() - toCopy, m_Buffer.end());
        }
        
        // 如果不足，填充零
        if (samples.size() < count) {
            samples.insert(samples.begin(), count - samples.size(), 0.0f);
        }
        
        return samples;
    }

}
