# EchoEar Display Bring-Up Notes

Date: 2026-03-08
Repo: `mimiclaw`
Board: EchoEar
Panel: ST77916, 360x360, QSPI

## Summary

The EchoEar display is now powered, initialized, backlit, and able to render the boot fill color correctly.

This bring-up aligned the project with the working EchoEar implementation from `xiaozhi-esp32`.

## Root Causes

1. No EchoEar-specific ST77916 vendor init sequence was applied.
2. EchoEar uses runtime PCB detection to choose the LCD reset pin.
3. EchoEar V1.0 and V1.2 use different reset polarity behavior.
4. `POWER_CTRL` polarity did not match the working board implementation.
5. Backlight behavior was closer to a simple GPIO switch than the working PWM setup.
6. RGB565 solid fill data needed byte swapping for the current panel path.

## Fix Order

1. Enabled EchoEar display power and backlight during startup.
2. Added runtime PCB detection and LCD reset pin selection.
3. Applied reset pulses with version-aware active level handling.
4. Injected the working ST77916 vendor init command table.
5. Switched backlight configuration to PWM LEDC.
6. Moved IMU init after display init to avoid early GPIO47 interference on V1.2 boards.
7. Tuned panel color/orientation flags until the quadrant test pattern matched expectations.
8. Fixed RGB565 fill byte order so the boot color renders correctly.
9. Removed the temporary quadrant test and restored the normal boot fill.

## Final Working Settings

File: `main/conf/esp_panel_board_custom_conf.h`

- `ESP_PANEL_BOARD_LCD_COLOR_BGR_ORDER = 0`
- `ESP_PANEL_BOARD_LCD_COLOR_INEVRT_BIT = 1`
- `ESP_PANEL_BOARD_LCD_SWAP_XY = 1`
- `ESP_PANEL_BOARD_BACKLIGHT_TYPE = ESP_PANEL_BACKLIGHT_TYPE_PWM_LEDC`
- EchoEar ST77916 vendor init command table is defined

File: `main/hardware/echoear_config.h`

- `ECHOEAR_POWER_CTRL_ON_LEVEL = 0`

File: `main/display/display_panel.cpp`

- Runtime EchoEar power setup
- Runtime EchoEar PCB config loading
- Version-aware LCD reset pulse
- RGB565 fill byte swap before DMA draw
- Boot logo reference orientation locked in as the validated upright baseline
- Future image/UI orientation work should use this baseline while `ESP_PANEL_BOARD_LCD_SWAP_XY = 1`

## Files Changed

- `main/CMakeLists.txt`
- `main/conf/esp_panel_board_custom_conf.h`
- `main/display/display_panel.cpp`
- `main/hardware/echoear_config.c`
- `main/hardware/echoear_config.h`
- `main/mimi.c`

## Validation

Build command:

```powershell
cmake --build build
```

Result:

- Build completed successfully
- `build/mimiclaw.bin` generated

## Follow-Up

Next recommended step is to apply the same confirmed color handling to any future RGB565 image, font, or UI drawing path, not only the boot solid-fill path.

## Orientation Baseline

The boot logo orientation confirmed by on-device validation is now the project reference orientation.

Reference rules:

- Keep `ESP_PANEL_BOARD_LCD_SWAP_XY = 1`
- Treat the transform in `main/display/display_panel.cpp` boot logo draw path as the upright display-space baseline
- Future bitmap, font, and UI layout adjustments should be measured against this validated baseline, not against raw source image orientation
