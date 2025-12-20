#version 330

// Glassmorphism 2.0 - 高级玻璃拟态着色器
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float uTime;
uniform float uIntensity; // 模糊/毛玻璃强度 (0.0 - 1.0)
uniform vec2 uResolution; // 窗口分辨率
uniform float uRoundness; // 圆角半径 (像素)

// 随机噪声函数，用于创建毛玻璃的“磨砂”感
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec2 uv = fragTexCoord;
    
    // 1. 基础采样
    vec4 texelColor = texture(texture0, uv);
    
    // 2. 模拟磨砂质感 (Frosted Texture)
    // 通过在 UV 上添加极其微小的随机偏移，模拟光影在粗糙表面的散射
    float noise = hash(uv + uTime * 0.01) * 0.04 * uIntensity;
    vec3 frosted = texture(texture0, uv + vec2(noise)).rgb;
    
    // 3. 边缘高光 (Edge Glow)
    // 简单的内发光模拟，增强厚度感
    float edge = 1.0 - smoothstep(0.48, 0.5, length(uv - 0.5));
    float borderGlow = pow(1.0 - length(uv - 0.5) * 2.0, 3.0) * 0.1;
    
    // 4. 混合最终颜色
    // 增加一点白色提亮使之更像玻璃，而不是单纯的半透明
    vec3 color = mix(frosted, vec3(1.0), 0.05 + borderGlow);
    
    // --- SDF 圆角剪裁 ---
    vec2 p = uv * uResolution;
    vec2 b = uResolution * 0.5;
    vec2 d = abs(p - b) - b + vec2(uRoundness);
    float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - uRoundness;
    if (dist > 0.0) discard;

    finalColor = vec4(color, texelColor.a) * colDiffuse * fragColor;
}
