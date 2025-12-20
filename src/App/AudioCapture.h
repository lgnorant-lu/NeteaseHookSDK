#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <vector>
#include <deque>
#include <mutex>
#include <atomic>

// 前向声明 miniaudio 生成实现
extern "C" {
    struct ma_device;
}

namespace Netease {
    class AudioCapture {
    public:
        static AudioCapture& Instance() {
            static AudioCapture instance;
            return instance;
        }

        bool Start();
        void Stop();

        // 获取最新的音频采样数据 (为了性能，返回原始 float 数据)
        std::vector<float> GetSamples(size_t count);

    private:
        AudioCapture();
        ~AudioCapture();

        static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, unsigned int frameCount);
        void OnDataInternal(const float* pInput, unsigned int frameCount);

        ma_device* m_pDevice = nullptr;
        std::deque<float> m_Buffer;  // 使用 deque 以获得 O(1) 的头部删除性能
        std::mutex m_BufferMutex;
        std::atomic<bool> m_IsRunning{false};
        
        static constexpr size_t BUFFER_SIZE = 4096;
    };
}

#endif // AUDIO_CAPTURE_H
