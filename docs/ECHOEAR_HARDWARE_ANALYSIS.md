# EchoEar 硬件配置分析

**分析时间**: 2026-03-07
**来源**: xiaozhi-esp32 项目 (github.com/78/xiaozhi-esp32)

---

## 硬件概述

**EchoEar 喵伴** 是一款智能 AI 开发套件，核心配置：

- **主控**: ESP32-S3-WROOM-1 模组
- **屏幕**: 1.85 寸 QSPI 圆形触摸屏 (360x360, ST77916 驱动)
- **音频输入**: 双麦阵列 (ES7210 ADC)
- **音频输出**: 扬声器 (ES8311 DAC)
- **触摸**: CST816S 电容触摸 IC
- **其他**: 充电管理、温度传感器、USB UVC 摄像头支持

---

## GPIO 引脚配置

### 1. 音频系统 (I2S)

#### I2S 主接口
```c
AUDIO_I2S_GPIO_MCLK  = GPIO_NUM_42   // Master Clock
AUDIO_I2S_GPIO_WS    = GPIO_NUM_39   // Word Select (LRCK)
AUDIO_I2S_GPIO_BCLK  = GPIO_NUM_40   // Bit Clock
AUDIO_I2S_GPIO_DOUT  = GPIO_NUM_41   // Data Out (to Speaker)
```

#### I2S 数据输入 (版本差异)
```c
// PCB V1.0
AUDIO_I2S_GPIO_DIN_1 = GPIO_NUM_15   // Data In (from Mic)
AUDIO_CODEC_PA_PIN_1 = GPIO_NUM_4    // Power Amplifier Enable

// PCB V1.2
AUDIO_I2S_GPIO_DIN_2 = GPIO_NUM_3    // Data In (from Mic)
AUDIO_CODEC_PA_PIN_2 = GPIO_NUM_15   // Power Amplifier Enable
```

#### I2C 音频编解码器
```c
AUDIO_CODEC_I2C_SDA_PIN = GPIO_NUM_2
AUDIO_CODEC_I2C_SCL_PIN = GPIO_NUM_1
AUDIO_CODEC_ES8311_ADDR = 0x18       // DAC (Speaker)
AUDIO_CODEC_ES7210_ADDR = 0x40       // ADC (Microphone)
```

#### 音频参数
```c
AUDIO_INPUT_SAMPLE_RATE  = 24000 Hz
AUDIO_OUTPUT_SAMPLE_RATE = 24000 Hz
AUDIO_INPUT_REFERENCE    = true      // 启用回声消除参考信号
```

---

### 2. 显示系统 (QSPI)

#### ST77916 LCD 驱动 (QSPI 模式)
```c
QSPI_LCD_HOST           = SPI2_HOST
QSPI_PIN_NUM_LCD_PCLK   = GPIO_NUM_18   // Clock
QSPI_PIN_NUM_LCD_CS     = GPIO_NUM_14   // Chip Select
QSPI_PIN_NUM_LCD_DATA0  = GPIO_NUM_46   // Data 0
QSPI_PIN_NUM_LCD_DATA1  = GPIO_NUM_13   // Data 1
QSPI_PIN_NUM_LCD_DATA2  = GPIO_NUM_11   // Data 2
QSPI_PIN_NUM_LCD_DATA3  = GPIO_NUM_12   // Data 3
QSPI_PIN_NUM_LCD_BL     = GPIO_NUM_44   // Backlight (PWM)
```

#### 复位引脚 (版本差异)
```c
QSPI_PIN_NUM_LCD_RST_1  = GPIO_NUM_3    // PCB V1.0
QSPI_PIN_NUM_LCD_RST_2  = GPIO_NUM_47   // PCB V1.2
```

#### 屏幕参数
```c
DISPLAY_WIDTH       = 360
DISPLAY_HEIGHT      = 360
DISPLAY_BIT_PER_PIXEL = 16 (RGB565)
DISPLAY_MIRROR_X    = false
DISPLAY_MIRROR_Y    = false
DISPLAY_SWAP_XY     = false
```

---

### 3. 触摸系统

#### CST816S 触摸 IC (I2C)
```c
TP_PORT          = I2C_NUM_1 (复用音频 I2C)
TP_I2C_ADDR      = 0x15
TP_PIN_NUM_INT   = GPIO_NUM_10   // 触摸中断
TP_PIN_NUM_RST   = GPIO_NUM_NC   // 无复位引脚
```

---

### 4. 按钮和控制

```c
BOOT_BUTTON_GPIO = GPIO_NUM_0    // Boot 按钮
POWER_CTRL       = GPIO_NUM_9    // 电源控制
LED_G            = GPIO_NUM_43   // 绿色 LED
```

---

### 5. 其他外设

#### UART (版本差异)
```c
// PCB V1.0
UART1_TX_1 = GPIO_NUM_6
UART1_RX_1 = GPIO_NUM_5

// PCB V1.2
UART1_TX_2 = GPIO_NUM_5
UART1_RX_2 = GPIO_NUM_4
```

#### SD 卡 (SPI)
```c
SD_MISO = GPIO_NUM_17
SD_SCK  = GPIO_NUM_16
SD_MOSI = GPIO_NUM_38
```

#### 充电管理
```c
CHARGE_IC_ADDR = 0x55 (I2C)
```

#### 编解码器电源控制
```c
CORDEC_POWER_CTRL = GPIO_NUM_48  // V1.2 需要拉高使能
```

---

## 音频架构

### 硬件拓扑

```
双麦克风 → ES7210 (ADC) → I2S_DIN → ESP32-S3
                                        ↓
                                   音频处理
                                   (AEC/VAD)
                                        ↓
ESP32-S3 → I2S_DOUT → ES8311 (DAC) → 功放 → 扬声器
```

### 音频编解码器

**ES7210** (ADC - 麦克风输入)
- I2C 地址: 0x40
- 双通道 ADC
- 支持回声消除参考信号

**ES8311** (DAC - 扬声器输出)
- I2C 地址: 0x18
- 单声道 DAC
- 内置功放控制

### 音频处理流程 (xiaozhi-esp32)

```
AudioInputTask (Core 0)
    ↓
AudioCodec::Read() → 原始 PCM (24kHz)
    ↓
AudioProcessor (AEC/VAD) → 清洁 PCM
    ↓
audio_encode_queue_
    ↓
OpusCodecTask (Core 1)
    ↓
OpusEncoder → Opus 数据包
    ↓
audio_send_queue_ → 发送到服务器
```

---

## 显示架构

### 驱动芯片: ST77916

- **接口**: QSPI (4 线数据)
- **分辨率**: 360x360
- **颜色深度**: RGB565 (16-bit)
- **刷新率**: 60Hz
- **特性**: 圆形显示区域

### 初始化序列

xiaozhi-esp32 提供了完整的 ST77916 初始化命令序列 (200+ 条寄存器配置)，包括：
- 电源管理
- Gamma 校正
- 时序配置
- 显示方向

### 背光控制

- PWM 控制 (GPIO_44)
- 支持亮度调节
- 自动保存亮度设置

---

## 触摸交互

### CST816S 功能

- **分辨率**: 360x360
- **触摸点**: 单点触摸
- **手势**: 支持滑动、长按检测
- **中断**: 下降沿触发 (GPIO_10)

### 触摸事件

```c
TOUCH_PRESS   // 按下
TOUCH_RELEASE // 释放
TOUCH_HOLD    // 长按
```

---

## PCB 版本检测

EchoEar 有两个硬件版本，通过 I2C 探测自动识别：

```c
// 检测逻辑
if (i2c_probe(0x18) == OK) {
    // V1.0: ES8311 直接可访问
} else {
    gpio_set_level(GPIO_48, HIGH);  // 使能编解码器电源
    if (i2c_probe(0x18) == OK) {
        // V1.2: 需要先使能电源
        // 切换到 V1.2 引脚定义
    }
}
```

---

## 电源管理

### 充电管理 IC

- I2C 地址: 0x55
- 功能: 电池电压/电流监测
- 轮询周期: 300ms

### 温度传感器

- ESP32-S3 内置温度传感器
- 范围: 10-50°C
- 用于过热保护

---

## 关键技术细节

### 1. I2S 配置

```c
i2s_config_t i2s_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX,
    .sample_rate = 24000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = true,  // 使用 APLL 提高精度
    .tx_desc_auto_clear = true,
};
```

### 2. QSPI 显示优化

- DMA 传输: `QSPI_LCD_H_RES * 80 * sizeof(uint16_t)`
- 双缓冲: 减少撕裂
- 局部刷新: 仅更新变化区域

### 3. 触摸中断处理

- 使用 FreeRTOS 信号量
- 独立任务处理触摸事件 (Core 1, Priority 5)
- 防抖动: 检测 PRESS/RELEASE 状态变化

---

## 与 MimiClaw 的集成建议

### 1. 复用现有模块

- **按钮驱动**: MimiClaw 已有 `button_driver.c`，可直接使用
- **I2C 驱动**: 复用 `imu/I2C_Driver.c`
- **消息总线**: 使用 `message_bus.c` 传递音频/显示事件

### 2. 新增模块

```
main/
├── hardware/
│   └── echoear_config.h       # 硬件配置集中管理
├── display/
│   ├── st77916_driver.c       # ST77916 QSPI 驱动
│   ├── display_manager.c      # 显示内容管理
│   └── ui_widgets.c           # UI 组件
├── audio/
│   ├── es8311_codec.c         # ES8311 DAC 驱动
│   ├── es7210_codec.c         # ES7210 ADC 驱动
│   ├── audio_input.c          # 录音管理
│   ├── audio_output.c         # 播放管理
│   └── vad.c                  # 语音活动检测
└── touch/
    └── cst816s_driver.c       # 触摸驱动
```

### 3. 内存分配策略

- **音频缓冲区**: PSRAM (32KB x 4)
- **显示缓冲区**: PSRAM (360x80x2 = 57.6KB x 2)
- **触摸队列**: Internal SRAM (小数据)

---

## 参考资源

- **xiaozhi-esp32 项目**: https://github.com/78/xiaozhi-esp32
- **EchoEar 硬件**: https://oshwhub.com/esp-college/echoear
- **ST77916 驱动**: ESP-IDF esp_lcd_st77916 组件
- **ES8311 驱动**: ESP-ADF audio_hal 组件
- **CST816S 驱动**: ESP-IDF esp_lcd_touch_cst816s 组件

---

## 下一步行动

1. 创建 `main/hardware/echoear_config.h` 配置文件
2. 实现 ST77916 显示驱动
3. 实现 ES8311/ES7210 音频驱动
4. 集成到 MimiClaw Agent Loop
