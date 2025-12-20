#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <vector>
#include <cmath>
#include "raylib.h"
#include "raymath.h"
#include "FftHelper.h"

namespace Netease {
    struct Particle {
        Vector2 position;
        Vector2 velocity;
        float life;
        Color color;
    };

    class Visualizer {
    public:
        static Visualizer& Instance() {
            static Visualizer instance;
            return instance;
        }

        void Update(const std::vector<float>& magnitudes, float deltaTime) {
            // 安全检查：空数据
            if (magnitudes.empty()) return;
            
            // 1. 平滑处理频带数据
            float currentEnergy = 0;
            if (m_Bands.size() != magnitudes.size()) {
                m_Bands.assign(magnitudes.size(), 0.0f);
            }
            
            for (size_t i = 0; i < magnitudes.size(); i++) {
                currentEnergy += magnitudes[i];
                if (magnitudes[i] > m_Bands[i]) {
                    m_Bands[i] = magnitudes[i];
                } else {
                    m_Bands[i] = Lerp(m_Bands[i], magnitudes[i], 0.15f);
                }
            }
            m_EnergyPulse = Lerp(m_EnergyPulse, currentEnergy / (float)magnitudes.size(), 0.2f);

            // 2. 更新粒子系统
            // ... (粒子更新逻辑保持不变)
            for (int i = (int)m_Particles.size() - 1; i >= 0; i--) {
                m_Particles[i].position.x += m_Particles[i].velocity.x * deltaTime;
                m_Particles[i].position.y += m_Particles[i].velocity.y * deltaTime;
                m_Particles[i].life -= deltaTime;
                if (m_Particles[i].life <= 0) {
                    m_Particles.erase(m_Particles.begin() + i);
                }
            }

            // 3. 高能触发粒子
            if (currentEnergy > 5.0f && m_Particles.size() < 120) {
                EmitParticle();
            }
        }

        float GetEnergyPulse() const { return m_EnergyPulse; }

        void Draw(int width, int height, Color primaryColor) {
            if (m_Bands.empty()) return;
            m_LastWidth = width;
            m_LastHeight = height;

            float time = (float)GetTime();
            
            // 4. 绘制流体丝线 (多层叠加)
            int bandCount = (int)m_Bands.size();
            float spacing = (float)width / (bandCount - 1);

            BeginBlendMode(BLEND_ADDITIVE);
            
            // 定义层颜色 (低频深，高频亮)
            Color layerColors[] = {
                { 150, 40, 40, 255 },   // 深红 (Bass)
                primaryColor,           // 主题色 (Mids) - THEME_RED
                { 255, 180, 0, 255 }    // 橙黄 (Highs)
            };

            for (int layer = 0; layer < 3; layer++) {
                std::vector<Vector2> points;
                float alphaScale = 0.5f - layer * 0.12f;
                
                // 动态计算绘制区域
                float drawWidth = (float)width;
                float startX = 0;
                if (width > 600) {
                    startX = width * 0.5f;
                    drawWidth = width * 0.5f;
                }
                float localSpacing = drawWidth / (bandCount - 1);

                for (int i = 0; i < bandCount; i++) {
                    float energy = (layer == 0) ? m_Bands[i % 8] : 
                                  (layer == 2) ? m_Bands[bandCount - 1 - (i % 8)] : m_Bands[i];

                    float gain = 180.0f + layer * 40.0f;
                    float wave = sinf(time * 2.2f + i * 0.4f + layer) * 15.0f;
                    float h = energy * gain;
                    
                    float x = startX + i * localSpacing;
                    float yBase = (height > 300) ? (height * 0.82f) : (height * 0.65f);
                    float y = yBase - h + wave; 
                    points.push_back({ x, y });
                }

                for (int i = 0; i < (int)points.size() - 1; i++) {
                    float thickness = 3.2f - layer * 0.8f;
                    DrawLineBezier(points[i], points[i+1], thickness, ColorAlpha(layerColors[layer], alphaScale));
                }
            }
            EndBlendMode();

            // 5. 绘制粒子 ("点点")
            BeginBlendMode(BLEND_ADDITIVE);
            for (const auto& p : m_Particles) {
                float pAlpha = (p.life > 0.4f) ? 1.0f : p.life * 2.5f;
                if (sinf(time * 25.0f + p.life * 12.0f) > 0.0f) {
                    DrawCircleGradient((int)p.position.x, (int)p.position.y, 1.2f + p.life * 1.5f, ColorAlpha(p.color, pAlpha), BLANK);
                }
            }
            EndBlendMode();
        }

    private:
        Visualizer() {}

        void EmitParticle() {
            if (m_LastWidth <= 0 || m_LastHeight <= 0) return;
            Particle p;
            float startX = (m_LastWidth > 600) ? m_LastWidth * 0.5f : 0;
            float endX = (float)m_LastWidth;
            
            p.position = { (float)GetRandomValue((int)startX, (int)endX), (float)GetRandomValue((int)(m_LastHeight * 0.5f), (int)(m_LastHeight * 0.9f)) };
            p.velocity = { (float)GetRandomValue(-30, 30), (float)GetRandomValue(-100, -50) };
            p.life = (float)GetRandomValue(5, 12) / 10.0f;
            p.color = { 255, (unsigned char)GetRandomValue(150, 250), (unsigned char)GetRandomValue(0, 150), 200 };
            m_Particles.push_back(p);
        }

        std::vector<float> m_Bands;
        std::vector<Particle> m_Particles;
        float m_EnergyPulse = 0.0f;
        int m_LastWidth = 0;
        int m_LastHeight = 0;
    };
}

#endif // VISUALIZER_H
