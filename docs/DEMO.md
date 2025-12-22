# NeteaseMonitor Demo - 技术规格书

**版本**: v0.1.3 
**类型**: 技术展示应用 (SDK功能演示)  
**框架**: Raylib 5.6-dev  
**语言**: C++17  
**平台**: Windows x86/x64

---

## 1. 架构总览

NeteaseMonitor 是基于 NeteaseHookSDK 构建的轻量级可视化监控工具，采用**实时渲染 + 异步数据加载**的混合架构。

```
┌─────────────────────────────────────────────────────────┐
│                   NeteaseMonitor.exe                     │
├──────────────┬──────────────────┬──────────────────────┤
│ UI Layer     │ Audio Layer      │ Data Layer           │
│ - Raylib     │ - WASAPI         │ - NeteaseDriver      │
│ - Shaders    │ - FFT (1024)     │ - NeteaseAPI         │
│ - Physics    │ - Visualizer     │ - AlbumCover Cache   │
└──────────────┴──────────────────┴──────────────────────┘
```

### 1.1 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| **主循环** | `main.cpp` | 窗口管理、事件分发、渲染调度 |
| **封面系统** | `AlbumCover.h/cpp` | 封面下载、纹理缓存 (LRU-10) |
| **音频捕获** | `AudioCapture.h/cpp` | WASAPI Loopback、环形缓冲 |
| **FFT分析** | `FftHelper.h` | 快速傅里叶变换、频段映射 |
| **可视化** | `Visualizer.h` | 粒子系统、流体丝线渲染 |
| **内存监控** | `MemoryMonitor.h/cpp` | Windows PSAPI 隔离调用 |

---

## 2. 渲染管线 (Rendering Pipeline)

### 2.1 帧渲染流程

```
BeginDrawing()
  ↓
[1] ClearBackground(TRANSPARENT)
  ↓
[2] BeginShaderMode(g_AuroraShader)  ← 极光背景层
     DrawRectangleRec(fullscreen)
     EndShaderMode()
  ↓
[3] BeginShaderMode(g_GlassShader)   ← 玻璃拟态层
     DrawRectangleRounded(mainWindow, roundness=0.06, segments=48)
     EndShaderMode()
  ↓
[4] Visualizer::Draw()               ← 音频可视化层
     - DrawLineEx(流体丝线 x3)
     - DrawCircleV(粒子 x120)
  ↓
[5] BeginShaderMode(g_MaskShader)    ← 唱片遮罩层
     DrawTextureEx(coverTexture)
     EndShaderMode()
  ↓
[6] DrawUI()                         ← UI文本层
     - DrawTextEx(标题/艺术家)
     - DrawTextEx(歌词 x5行)
     - DrawRectangleRec(进度条)
  ↓
[7] DrawDebugOverlay() [DEBUG模式]   ← 调试层
     - DrawText(内存占用)
     - DrawText(FPS)
  ↓
EndDrawing()
```

### 2.2 Shader规格

#### `aurora.fs` (极光背景)
```glsl
输入:
  - uTime: float        // 时间 (秒)
  - uEnergy: float      // 音频能量 (0.0-1.0)
  - uColor1: vec3       // 主题色1 (RGB)
  - uColor2: vec3       // 主题色2 (RGB)
  - uRoundness: float   // 圆角比例 (0.06)

算法:
  1. Simplex噪声生成 (3层叠加)
  2. 时间驱动波动 (speed = 0.2 + energy * 0.3)
  3. SDF圆角裁剪 (smoothstep边缘)

输出: vec4 (RGB + Alpha)
```

#### `glass.fs` (玻璃拟态)
```glsl
输入:
  - uTime: float
  - uIntensity: float   // 0.8 + energy * 0.4

算法:
  1. Noise纹理生成 (伪随机)
  2. 边缘内发光 (distance-based)
  3. SDF圆角裁剪

特性: 动态强度调制 (随音乐变化)
```

#### `circle_mask.fs` (圆形遮罩)
```glsl
输入:
  - uAngle: float       // 唱片旋转角度 (弧度)

算法:
  1. UV坐标归一化至 [-1, 1]
  2. 距离场计算: dist = length(uv)
  3. 圆形裁剪: alpha = 1.0 - smoothstep(0.48, 0.5, dist)

应用: 封面纹理圆形遮罩
```

---

## 3. 音频可视化系统

### 3.1 信号采集 (AudioCapture)

**技术栈**: Windows WASAPI Loopback + Miniaudio

```cpp
配置参数:
  - 采样率: 48000 Hz
  - 格式: Float32
  - 通道: Stereo (混合后取均值)
  - 缓冲: std::deque<float> (动态, O(1) pop_front)
  - 容量: 16384 samples (约340ms @48kHz)
```

**回调流程**:
```
DataCallback(frameCount)
  ↓
[1] 遍历帧: for (i = 0; i < frameCount)
  ↓
[2] 混合立体声: mono = (left + right) / 2.0f
  ↓
[3] 推入缓冲: m_Buffer.push_back(mono)
  ↓
[4] 容量控制: if (size > 16384) pop_front()
```

### 3.2 频谱分析 (FftHelper)

**算法**: Cooley-Tukey FFT (1024点)

```cpp
处理流程:
[1] 获取1024样本 → samples[1024]
[2] 应用Hann窗   → samples[i] *= 0.5 * (1 - cos(2π*i/N))
[3] FFT变换      → complex[512] (仅正频率)
[4] 计算幅度     → magnitudes[i] = sqrt(real² + imag²)
[5] 频段映射     → CalculateBands(32) // 32个频段
```

**频段分布** (对数映射):
```
低频 (20-250 Hz):     频段 0-7   (人声基频)
中频 (250-2000 Hz):   频段 8-19  (乐器主体)
高频 (2000-20000 Hz): 频段 20-31 (泛音/打击乐)
```

### 3.3 可视化渲染 (Visualizer)

**粒子系统**:
```cpp
常量:
  - MAX_PARTICLES = 120
  - SPAWN_RATE = 2 (每帧)
  - LIFETIME = 2.0秒
  - GRAVITY = 200.0 px/s²

动力学:
  position += velocity * dt
  velocity.y += GRAVITY * dt
  alpha = 1.0 - (age / lifetime)
```

**流体丝线** (3层):
```cpp
Layer 1 (红色): baseHeight + magnitudes[i] * 80
Layer 2 (青色): baseHeight + magnitudes[i] * 60
Layer 3 (橙色): baseHeight + magnitudes[i] * 40

绘制: DrawLineEx(p1, p2, thickness=2.0, color)
```

---

## 4. 数据管理

### 4.1 封面缓存策略 (AlbumCover)

**LRU Cache**:
```cpp
容量: 10张
策略: Least Recently Used
结构: std::unordered_map<long long, CacheEntry>

struct CacheEntry {
    Texture2D texture;          // OpenGL纹理
    unsigned long long lastUse; // 时间戳 (GetTickCount64)
};
```

**下载流程**:
```
GetTexture(songId, coverUrl)
  ↓
[1] 缓存查询 → 命中? 返回纹理
  ↓ 未命中
[2] WinINet下载 → std::vector<BYTE> imageData
  ↓
[3] stb_image解码 → Image (w, h, RGBA)
  ↓
[4] 降采样 → ImageResize(&img, 1024, 1024)
  ↓
[5] 上传GPU → LoadTextureFromImage(img)
  ↓
[6] LRU淘汰 → if (cache.size() >= 10) evict_oldest()
  ↓
[7] 插入缓存 → cache[songId] = {texture, now()}
```

### 4.2 歌词解析 (ParseLrcToMap)

**LRC格式**:
```
[00:12.50]歌词内容
[mm:ss.xx]text
```

**解析器** (正则表达式):
```cpp
regex pattern: R"(\[(\d+):(\d+\.\d+)\](.*)\n)"

解析流程:
  std::regex_iterator → match
    ↓
  提取: minutes, seconds, text
    ↓
  计算: timestamp = min*60 + sec
    ↓
  存储: map<double, string>[timestamp] = text
```

**合并翻译**:
```cpp
for (auto& [time, lrc] : lrcMap) {
    if (tlyricMap.count(time)) {
        lrc += " / " + tlyricMap[time];
    }
}
```

---

## 5. 物理系统

### 5.1 唱片旋转 (DiscRotation)

**参数**:
```cpp
TARGET_RPM = 1.2         // 转速 (转/分) - 经过网易云风格微调
ACCEL = 45.0 °/s²        // 加速度
DECEL = 30.0 °/s²        // 减速度
```

**PID简化版**:
```cpp
void Update(isPlaying, dt) {
    targetV = isPlaying ? (1.2/60*360) : 0;  // 7.2°/s or 0
    accel = isPlaying ? ACCEL : DECEL;
    
    diff = targetV - angularVelocity;
    step = sign(diff) * accel * dt;
    
    if (abs(step) > abs(diff))
        angularVelocity = targetV;
    else
        angularVelocity += step;
    
    angle += angularVelocity * dt;
    angle %= 360.0;
}
```

### 5.2 唱针机械 (Tonearm)

**状态机**:
```
ANGLE_UP = -45°   (待机)
ANGLE_DOWN = -15° (播放)
MOVE_SPEED = 90°/s

Update(isPlaying, dt):
  target = isPlaying ? DOWN : UP
  diff = target - angle
  
  if (abs(diff) < SPEED*dt)
    angle = target
  else
    angle += sign(diff) * SPEED * dt
```

### 5.3 窗口布局 (PhysicsLayout)

**弹性插值**:
```cpp
Spring系数 k = 12.0

Update(dt):
  currentW += (targetW - currentW) * k * dt
  currentH += (targetH - currentH) * k * dt
  
  if (abs(currentW - targetW) < 0.1)
    SetWindowSize(currentW, currentH)
```

---

## 6. 性能分析

### 6.1 内存占用 (实测)

```
启动时:       ~60 MB
加载首歌:     ~80 MB
播放中:       ~95 MB
切换5首后:    ~97 MB
长时间运行:   ~100 MB (30分钟稳定)
```

**内存分布**:
```
字体纹理:     32 MB  (8192x4096 GRAY_ALPHA)
封面缓存:     30 MB  (10张 1024x1024 RGBA)
Shader资源:   5 MB   (编译后代码 + uniforms)
AudioCapture: 64 KB  (16384 samples * 4 bytes)
其他:         30 MB  (堆/栈/Raylib内部)
```

### 6.2 性能指标

**渲染**:
```
目标帧率: 60 FPS
实测帧率: 60 FPS (稳定, VSync开启)
帧预算:   16.67 ms/frame

时间分布:
  - Audio FFT:    0.5 ms
  - Shader渲染:   2.0 ms
  - UI绘制:       1.5 ms
  - 其他:         0.5 ms
  - 空闲:        12.17 ms
```

**加载性能**:
```
歌曲切换总耗时: ~2秒
  - Metadata API:  500 ms
  - Lyric API:     650 ms
  - Cover下载:     850 ms
  - 并发加载:     否 (顺序执行)
```

### 6.3 优化技术

1. **deque缓冲** - O(1) pop_front (替代vector)
2. **LRU淘汰** - 限制纹理内存
3. **Shader预编译** - 启动时加载
4. **UIConstants** - 减少魔法数字计算
5. **ScissorMode** - 限制可视化绘制区域 (待实现)

---

## 7. 技术规范

### 7.1 代码风格

**注释规范**: 中文 (80%+覆盖率)
**命名约定**: 
- 类: `PascalCase`
- 函数: `PascalCase`
- 变量: `camelCase`
- 全局: `g_Prefix`
- 常量: `UPPER_SNAKE_CASE`

### 7.2 依赖清单

| 依赖 | 版本 | 用途 |
|------|------|------|
| Raylib | 5.6-dev | 图形渲染 |
| Miniaudio | - | 音频采集 |
| stb_image | v2.30 | 图片解码 |
| WinINet | - | HTTP请求 |
| PSAPI | - | 内存监控 |

### 7.3 字符渲染系统 [v0.1.3]

**码点覆盖**: 21,391 个字符
- ASCII 基础字符 (32-126): 95 个
- **CJK 符号与标点 (0x3000-0x303F): 64 个** ⬅️ 新增
- CJK 统一汉字 (0x4E00-0x9FFF): 20,992 个
- **全角 ASCII 变体 (0xFF00-0xFFEF): 240 个** ⬅️ 新增

**字体加载优先级**:
1. `msyh.ttc` (微软雅黑) - Windows 7+ 默认
2. `simhei.ttf` (黑体) - 经典字体
3. `simsun.ttc` (宋体) - 兜底方案
4. `GetFontDefault()` - Raylib 默认（ASCII-only）

**修复问题**: 彻底解决歌词中全角标点符号（。？！）显示为 "?" 的乱码问题。

---

## 8. 已知限制

1. **单显示器** - 多显示器边界检测未实现
2. **硬编码字体路径** - `C:/Windows/Fonts/simhei.ttf`
3. **顺序加载** - 歌曲切换非并发加载
4. **Visualizer溢出** - 未启用ScissorMode裁剪

---

## 9. 编译与运行

### 编译
```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target NeteaseMonitor
```

### 运行前提
1. 安装 Hook: `Ctrl+I` (自动部署 version.dll)
2. 启动网易云音乐
3. 运行 `NeteaseMonitor.exe`

### 命令行参数 [v0.1.2 新增]
```powershell
# 默认静默模式（无控制台输出）
NeteaseMonitor.exe

# 启用详细日志
NeteaseMonitor.exe --verbose
NeteaseMonitor.exe -v

# 强制静默模式
NeteaseMonitor.exe --silent
NeteaseMonitor.exe -s

# 重定向日志到文件
NeteaseMonitor.exe --log=debug.log
```

**说明**:
- **默认模式**: SDK 内部使用物理级 `stderr` 重定向，彻底压制所有输出（包括第三方库）。
- **Verbose 模式**: 将日志输出至控制台，用于调试和问题追踪。
- **Log 模式**: 日志会自动重定向到指定文件，同时保持控制台洁净。

### 快捷键
```
Ctrl + I  : 安装 Hook
Ctrl + K  : 强制重启网易云
Ctrl + R  : 刷新安装路径
Left Drag : 拖拽窗口
```

---

## 10. 架构设计原则

1. **关注点分离**: UI / Audio / Data 三层解耦
2. **资源管理**: RAII + 显式清理
3. **错误容错**: API失败返回 nullopt, 不崩溃
4. **性能优先**: 60 FPS 硬约束
5. **可维护性**: 常量集中 + 详细注释

---

**文档版本**: 0.1.3  
**最后更新**: 2025-12-22  
**作者**: lgnorant-lu
