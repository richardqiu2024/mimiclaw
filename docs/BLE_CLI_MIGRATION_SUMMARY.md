# BLE CLI 改造结论（2026-03-06）

## 目标
- 将 `serial cli` 通道从串口改为蓝牙。
- 保持 `ESP_LOG`、panic、其他 debug 信息继续从串口输出。

## 结论
- 已完成：CLI 输入/输出通道改为 BLE（NimBLE GATT）。
- 已保持：日志与调试输出仍走 USB Serial/JTAG 串口控制台。
- CLI 命令集合保持不变（`set_wifi`、`set_tg_token`、`set_api_key`、`help` 等均保留）。

## 实现方式
- 将 `serial_cli` 重构为“命令核心层”（注册命令 + 执行命令行），不再启动串口 REPL。
- 新增 `ble_cli` 作为传输层：
  - 设备名：`MimiClaw-CLI`
  - BLE 写入命令（RX）+ 通知返回结果（TX）
  - 收到整行命令后调用 `serial_cli_run_line()` 执行

## 关键改动文件
- `main/cli/serial_cli.h`
- `main/cli/serial_cli.c`
- `main/cli/ble_cli.h`（新增）
- `main/cli/ble_cli.c`（新增）
- `main/mimi.c`（启动入口改为 `ble_cli_init()`）
- `main/CMakeLists.txt`（加入 `ble_cli.c` 与 `bt` 依赖）
- `sdkconfig.defaults.esp32s3`（启用 BLE/NimBLE 相关默认配置）
- `README.md`、`docs/ARCHITECTURE.md`（文档同步）

## 当前验证状态
- 代码改造已落地。
- 当前终端环境缺少 `idf.py` / `cmake`，未完成本地编译验证。

## 建议验证步骤
1. 在本机 ESP-IDF 环境执行：`idf.py fullclean build`
2. 烧录并连接 BLE 设备 `MimiClaw-CLI`
3. 通过 BLE 发送命令（以换行结束），确认收到命令回显与执行结果
4. 同时打开串口监视器，确认 `ESP_LOG` 仍正常输出
