# 微信小程序 BLE CLI 会话纪要（2026-03-06）

## 1. 目标
- 为 ESP32 MiniClaw 提供一个微信小程序，通过 BLE 与 `MimiClaw-CLI` 通讯。
- 在小程序内完成 CLI 命令发送、配置修改与设备操作。

## 2. 会话需求演进
1. 初始需求：实现微信小程序与 ESP32 BLE CLI 通讯，支持配置与命令操作。
2. 中间追加：终端日志必须实时更新，并始终显示最新数据。
3. 最后追加：WiFi CLI 增加静态 IP 配置后，小程序端也要同步支持。

## 3. 已完成内容
### 3.1 BLE 与 CLI 基础能力
- 扫描、连接、断开、重连 `MimiClaw-CLI`。
- 发现 Service/Characteristic 并订阅通知。
- CLI 发送支持分片写入（20 bytes）与发送队列。
- 终端显示命令与回显。

### 3.2 稳定性与可用性增强
- 补齐并统一连接状态管理（`setState`）。
- 修复 BLE 事件监听的注册/解绑处理，避免重复回调。
- 本地持久化上次连接设备 ID，支持“一键重连”。
- 命令超时机制增强，避免队列卡死。

### 3.3 配置面板能力
- 可视化配置并发送以下命令：
  - `set_wifi`
  - `set_api_key`
  - `set_tg_token`
  - `set_model`
  - `set_model_provider`
  - `config_show`
  - `config_reset`
  - `restart`

### 3.4 终端实时更新（按追加需求完成）
- 增加动态底部锚点，日志新增时自动滚动到底部。
- 增加未换行片段显示（`partialLine`），BLE 分片到达可实时可见。
- 断开/重连时清理半行缓存，防止旧数据残留。

### 3.5 静态 WiFi 配置（按追加需求完成）
- 新增表单字段：`IP`、`Mask`、`Gateway`、`DNS1`、`DNS2`。
- 新增命令按钮：
  - `set_wifi_static <ip> <mask> <gateway> [dns1] [dns2]`
  - `clear_wifi_static`
- 增加 IPv4 基础格式校验与输入约束提示。

## 4. 变更文件
- `wechat-miniapp/pages/index/index.js`
- `wechat-miniapp/pages/index/index.wxml`
- `wechat-miniapp/pages/index/index.wxss`
- `wechat-miniapp/README.md`

## 5. 校验记录
- 已执行语法检查：
  - `node --check wechat-miniapp/pages/index/index.js`
  - `node --check wechat-miniapp/app.js`
  - `node --check wechat-miniapp/utils/utf8.js`
- 检查结果：通过。

## 6. 使用说明（当前版本）
1. 在微信开发者工具导入 `wechat-miniapp/`。
2. 扫描并连接 `MimiClaw-CLI`。
3. 通过“快速配置”或终端输入发送命令。
4. 设置静态 IP 或清除静态 IP 后，发送 `restart` 使配置生效。

## 7. 后续可选优化
1. 命令历史与一键重发。
2. 连接成功后自动执行初始化命令（如 `config_show`）。
3. 多设备收藏与设备别名管理。
