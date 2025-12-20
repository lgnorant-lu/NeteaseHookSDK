#version 330

// 拟真黑胶唱片着色器
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float uTime;
uniform float uAngle; // 当前旋转角度 (弧度)

#define PI 3.14159265359

void main()
{
    vec2 uv = fragTexCoord;
    vec2 center = vec2(0.5, 0.5);
    vec2 rel = uv - center;
    float dist = length(rel);
    
    // 1. 圆形裁切
    if (dist > 0.5) discard;
    
    // 2. 基础纹理颜色
    vec4 texelColor = texture(texture0, uv);
    
    // 3. 模拟黑胶细纹 (Micro-grooves)
    // 根据到中心的距离产生极细微的明暗变化
    float grooves = sin(dist * 600.0) * 0.03;
    
    // 4. 各向同性高光 (Isotropic Highlight)
    // 这里的 V 字型反光取决于片段相对于圆心的角度
    float angle = atan(rel.y, rel.x);
    
    // 旋转背景参考系 (随唱片旋转偏移反光)
    float angleShift = uAngle * 0.2; 
    
    // 产生两个相对的 V 型高光带
    float highlight1 = pow(abs(sin(angle + angleShift)), 16.0) * 0.15;
    float highlight2 = pow(abs(cos(angle + PI/4.0 + angleShift)), 20.0) * 0.1;
    
    // 将高光与细纹叠加到颜色上
    vec3 color = texelColor.rgb;
    color += grooves;
    color += highlight1 + highlight2;
    
    // 如果是太中心的地方（圆孔），可以稍微调暗
    if (dist < 0.02) color *= 0.2;
    
    finalColor = vec4(color, texelColor.a) * colDiffuse * fragColor;
}
