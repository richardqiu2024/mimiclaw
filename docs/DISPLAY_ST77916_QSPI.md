# ST77916(QSPI) 集成说明

本项目已接入 `espressif/esp32_display_panel`（`1.*`），并使用自定义板级配置文件驱动 `ST77916` QSPI 屏。

## 关键文件

- 依赖声明：`main/idf_component.yml`
- 板级配置：`main/conf/esp_panel_board_custom_conf.h`
- 显示服务：`main/display/display_panel.h`
- 显示实现：`main/display/display_panel.cpp`
- 启动接入点：`main/mimi.c`
- 自定义头搜索路径：`CMakeLists.txt`

## 当前配置

- 分辨率：`360x360`
- 总线：`QSPI`（Host ID: `1`）
- 色深：`RGB565`
- 背光：`GPIO44`，高电平点亮
- 复位：默认 `GPIO3`（可通过 `MIMI_LCD_RST_GPIO` 切到 `GPIO47`）

## 当前行为

设备启动后尝试初始化显示，成功则渲染启动底色；失败不会阻塞主业务流程。

## 若要调整引脚

直接修改 `main/conf/esp_panel_board_custom_conf.h` 中对应宏并重新编译烧录。
