#version 330

// Aurora Background - 极光动态背景着色器
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform float uTime;
uniform vec3 uColor1; // 极光主色
uniform vec3 uColor2; // 极光次色
uniform float uEnergy; // 音乐能量脉冲 (0.0 - 1.0)
uniform vec2 uResolution; // 窗口分辨率
uniform float uRoundness; // 圆角半径 (像素)

// Simplex Noise 实现 (简化版)
vec3 permute(vec3 x) { return mod(((x*34.0)+1.0)*x, 289.0); }

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
                        -0.577350269189626, 0.024390243902439);
    vec2 i  = floor(v + dot(v, C.yy));
    vec2 x0 = v -   i + dot(i, C.xx);
    vec2 i1;
    i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute(permute( i.y + vec3(0.0, i1.y, 1.0 ))
                             + i.x + vec3(0.0, i1.x, 1.0 ));
    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy),
                            dot(x12.zw,x12.zw)), 0.0);
    m = m*m;
    m = m*m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 a0 = x - floor(x + 0.5);
    m *= 1.79284291400159 - 0.85373472095314 * (a0*a0 + h*h);
    vec3 g;
    g.x  = a0.x  * x0.x  + h.x  * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

void main()
{
    vec2 uv = fragTexCoord;
    
    // 时间偏移，随音乐能量加速
    float time = uTime * (0.2 + uEnergy * 0.8);
    
    // 采样多层噪声，模拟流动的极光
    float n1 = snoise(uv * 2.0 + vec2(time * 0.1, time * 0.05));
    float n2 = snoise(uv * 4.0 - vec2(time * 0.05, -time * 0.1));
    float n3 = snoise(uv * 1.5 + vec2(0.0, time * 0.02));
    
    float mask = smoothstep(0.1, 0.9, (n1 + n2 * 0.5 + n3 * 0.25) * 0.5 + 0.5);
    
    // 颜色渐变
    vec3 color = mix(uColor1, uColor2, uv.y + n3 * 0.2);
    color = mix(color * 0.2, color, mask);
    
    // --- SDF 圆角剪裁 ---
    vec2 p = uv * uResolution;
    vec2 b = uResolution * 0.5;
    vec2 d = abs(p - b) - b + vec2(uRoundness);
    float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - uRoundness;
    if (dist > 0.0) discard;

    finalColor = vec4(color, 0.45) * fragColor; // 基础透明度 0.45
}
