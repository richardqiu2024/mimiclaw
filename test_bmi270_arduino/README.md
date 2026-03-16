# BMI270 Arduino Test

Arduino测试程序，用于验证BMI270 IMU功能。

## 硬件配置

- **开发板**: ESP32-S3
- **I2C引脚**:
  - SDA: GPIO 2
  - SCL: GPIO 1
- **BMI270地址**: 0x68 或 0x69（自动检测）

## 测试方法

### 方法1: 简单I2C测试（test_bmi270_arduino.ino）

这个程序只测试I2C通信和chip ID读取，不需要额外的库。

1. 在Arduino IDE中打开 `test_bmi270_arduino.ino`
2. 选择开发板: ESP32S3 Dev Module
3. 上传并打开串口监视器（115200波特率）
4. 应该看到：
   ```
   BMI270 found at 0x68
   Chip ID: 0x24 (expected 0x24)
   BMI270 detected successfully!
   ```

### 方法2: 使用BMI270_AUX_BMM150库（test_with_library.ino）

这个程序使用完整的BMI270库，包括配置文件上传和数据读取。

1. **安装库**:
   - 下载 https://github.com/yahyatawil/BMI270_AUX_BMM150
   - Arduino IDE: Sketch -> Include Library -> Add .ZIP Library
   - 或者复制到 Arduino/libraries/ 目录

2. **上传程序**:
   - 打开 `test_with_library.ino`
   - 选择开发板: ESP32S3 Dev Module
   - 上传

3. **预期输出**:
   ```
   BMI270 Test with BMI270_AUX_BMM150 library
   Accel data Ready : X Y Z
   Gyro data Ready : X Y Z
   IMU initialized successfully!
   Accelerometer sample rate = 100 Hz

   Acceleration in G's
   X       Y       Z
   0.012   -0.034  0.987
   0.015   -0.032  0.989
   ...
   ```

## 故障排除

### 问题1: "BMI270 not found"
- 检查I2C接线（SDA=GPIO2, SCL=GPIO1）
- 检查BMI270电源
- 尝试降低I2C速度：`Wire.setClock(100000);`

### 问题2: "Failed to initialize IMU"
- 配置文件上传失败
- 检查I2C通信稳定性
- 增加I2C上拉电阻（如果没有）

### 问题3: "Invalid chip ID"
- I2C地址错误
- 硬件连接问题
- 检查BMI270是否正常供电

## 与ESP-IDF版本对比

如果Arduino版本工作正常，但ESP-IDF版本失败，说明问题在于：
1. ESP-IDF的I2C驱动配置
2. 配置文件上传的时序
3. 寄存器读写的延迟时间

Arduino库使用的关键参数：
- `read_write_len = 32` (32字节块)
- I2C速度: 400kHz
- 使用官方Bosch API的 `bmi270_init()` 函数
