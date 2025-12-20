#ifndef FFT_HELPER_H
#define FFT_HELPER_H

#include <vector>
#include <complex>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Netease {
    class FftHelper {
    public:
        // 执行快速傅里叶变换 (Radix-2 Cooley-Tukey)
        // input 大小必须为 2 的幂
        static std::vector<float> Analyze(const std::vector<float>& samples) {
            size_t n = samples.size();
            if (n == 0 || (n & (n - 1)) != 0) return {};

            std::vector<std::complex<double>> data(n);
            // 应用 Hann 窗并转为复数
            for (size_t i = 0; i < n; i++) {
                double window = 0.5 * (1.0 - cos(2.0 * M_PI * i / (n - 1)));
                data[i] = std::complex<double>(samples[i] * window, 0.0);
            }

            ComputeFFT(data);

            // 计算幅值 (只取前一半)
            size_t binCount = n / 2;
            std::vector<float> magnitudes(binCount);
            for (size_t i = 0; i < binCount; i++) {
                magnitudes[i] = (float)std::abs(data[i]) / (float)n;
            }

            return magnitudes;
        }

        // 将频率桶映射到有限数量的频带 (Bands)
        static std::vector<float> CalculateBands(const std::vector<float>& magnitudes, int bandCount) {
            if (magnitudes.empty() || bandCount <= 0) return std::vector<float>(bandCount > 0 ? bandCount : 1, 0.0f);
            
            std::vector<float> bands(bandCount, 0.0f);
            int binsPerBand = (int)magnitudes.size() / bandCount;
            if (binsPerBand <= 0) binsPerBand = 1; // 防止除零
            
            for (int i = 0; i < bandCount; i++) {
                float sum = 0;
                int start = i * binsPerBand;
                int actualBins = 0;
                for (int j = 0; j < binsPerBand && (start + j) < (int)magnitudes.size(); j++) {
                    sum += magnitudes[start + j];
                    actualBins++;
                }
                bands[i] = (actualBins > 0) ? (sum / actualBins) : 0.0f;
                
                // 增强型 Log 增益补偿，低频和高频都给予额外权重
                float boost = 1.0f + log10f((float)i + 1.0f) * 4.0f;
                if (i < 3) boost *= 2.5f; // 重低音增强
                if (i > bandCount - 5) boost *= 3.0f; // 高音爆点增强
                
                bands[i] *= boost;
            }
            
            return bands;
        }

    private:
        static void ComputeFFT(std::vector<std::complex<double>>& a) {
            size_t n = a.size();
            if (n <= 1) return;

            std::vector<std::complex<double>> a0(n / 2), a1(n / 2);
            for (size_t i = 0; i * 2 < n; i++) {
                a0[i] = a[i * 2];
                a1[i] = a[i * 2 + 1];
            }

            ComputeFFT(a0);
            ComputeFFT(a1);

            double ang = 2 * M_PI / n;
            std::complex<double> w(1), wn(cos(ang), sin(ang));
            for (size_t i = 0; i * 2 < n; i++) {
                a[i] = a0[i] + w * a1[i];
                a[i + n / 2] = a0[i] - w * a1[i];
                w *= wn;
            }
        }
    };
}


#endif // FFT_HELPER_H
