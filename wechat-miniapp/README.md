## MimiClaw BLE CLI Mini Program

这是一个微信小程序，用蓝牙 BLE 连接 `MimiClaw-CLI`，实现 CLI 方式配置设备。

### 功能
- 扫描并连接 `MimiClaw-CLI`
- CLI 命令输入与输出显示（带命令队列）
- 常用指令快捷按钮
- 可视化配置面板（WiFi / 静态IP / API Key / TG Token / Model / Provider）
- 断线后可一键重连（基于本地持久化设备 ID）

### BLE 设备信息
- 设备名：`MimiClaw-CLI`
- Service UUID：`6e400001-b5a3-f393-e0a9-500e24dcca9e`
- RX（写入）：`6e400002-b5a3-f393-e0a9-500e24dcca9e`
- TX（通知）：`6e400003-b5a3-f393-e0a9-500e24dcca9e`

### 常用 CLI 命令
```
set_wifi <ssid> <password>
set_wifi_static <ip> <mask> <gateway> [dns1] [dns2]
clear_wifi_static
set_api_key <key>
set_tg_token <token>
set_model <model>
set_model_provider <anthropic|openai>
config_show
config_reset
wifi_status
restart
```

### 使用说明
1. 在微信开发者工具导入 `wechat-miniapp/`。
2. 运行后点击“扫描”，选择 `MimiClaw-CLI` 连接。
3. 在“快速配置”里填写参数并发送，或在终端直接输入 CLI 命令（自动添加 `\n` 作为行结束）。
4. 需要完整命令列表时可发送 `help`。

### 重要提示
- iOS 默认写入包限制为 20 bytes，小程序已自动分片。
- 需要开启蓝牙权限及定位权限（iOS/Android 扫描需要定位）。
