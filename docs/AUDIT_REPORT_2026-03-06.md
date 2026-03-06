# MimiClaw 全项目审计报告（2026-03-06）

> 审计方式：Codex 全仓只读审计（未修改源码）  
> 审计范围：`/home/documents/mimiclaw`

## 项目现状

- 代码已形成按域分层的固件骨架（`bus` / `agent` / `llm` / `tools` / `memory` / `channel`），入口编排清晰（`main/CMakeLists.txt:1`、`main/mimi.c:124`）。
- 模块能力覆盖较完整：Telegram、WebSocket、BLE CLI、Cron、Heartbeat、Skills 已接入（`main/mimi.c:130`、`main/mimi.c:158`、`main/mimi.c:160`、`main/CMakeLists.txt:21`、`main/CMakeLists.txt:28`）。
- 构建发布链路可用（本地脚本 + GitHub Actions），但发布产物与工程配置存在偏差（`scripts/build_ubuntu.sh:1`、`.github/workflows/build.yml:19`、`.github/workflows/release.yml:32`、`CMakeLists.txt:9`）。
- 测试体系薄弱，CI 当前主要做编译校验，缺少单元/集成门禁（`.github/workflows/build.yml:19`）。
- 文档存在漂移（配置、能力矩阵、命令），可能误导维护与交付（`docs/ARCHITECTURE.md:229`、`docs/ARCHITECTURE.md:398`、`docs/TODO.md:87`、`.github/workflows/release.yml:82`）。

## 关键风险（按严重度排序）

1. **S1 安全基线不足**：默认硬编码 API Key + 外部入口缺少鉴权（Telegram/WS/BLE）。  
   证据：`main/mimi_config.h:19`、`main/telegram/telegram_bot.c:284`、`main/gateway/ws_server.c:70`、`main/cli/ble_cli.c:240`
2. **S1 `web_search` 实现与声明不符且不安全**：固定内网 IP + 明文 HTTP + 搜索密钥能力未充分体现。  
   证据：`main/tools/tool_web_search.c:160`、`main/tools/tool_web_search.c:260`、`main/tools/tool_web_search.c:48`、`README.md:218`
3. **S1 稳定性风险**：WiFi 事件回调存在阻塞式重试。  
   证据：`main/wifi/wifi_manager.c:209`、`main/wifi/wifi_manager.c:223`
4. **S2 发布一致性风险**：Release merged bin 与分区/说明存在偏差。  
   证据：`partitions.csv:7`、`.github/workflows/release.yml:37`、`.github/workflows/release.yml:82`、`main/cli/serial_cli.c:105`
5. **S2 会话上下文静默退化风险**：历史截断后解析失败会降级空历史。  
   证据：`main/memory/session_mgr.c:70`、`main/memory/session_mgr.c:118`、`main/agent/agent_loop.c:204`
6. **S3 可维护性风险**：大文件 + 文档漂移 + 测试缺失。  
   证据：`main/cli/serial_cli.c:1`、`main/llm/llm_proxy.c:1`、`docs/ARCHITECTURE.md:229`、`.github/workflows/build.yml:19`

## 改进建议

### P0（本周优先）

1. **安全入口与密钥治理**
   - 问题：默认密钥与弱鉴权。
   - 建议：移除默认 API Key；增加 Telegram allowlist、WS token、BLE 最小配对策略。
   - 预期收益：降低未授权调用与泄露风险。
   - 成本：中
   - 涉及文件：`main/mimi_config.h:19`、`main/telegram/telegram_bot.c:284`、`main/gateway/ws_server.c:70`、`main/cli/ble_cli.c:240`、`docs/TODO.md:36`

2. **`web_search` 可用性与安全修复**
   - 问题：硬编码地址、明文 HTTP、实现与 README 不一致。
   - 建议：切换到标准 HTTPS 搜索 API；host/port 可配置；保留代理但移除硬编码。
   - 预期收益：功能可部署、可审计。
   - 成本：中
   - 涉及文件：`main/tools/tool_web_search.c:160`、`main/tools/tool_web_search.c:260`、`main/tools/tool_web_search.c:142`、`README.md:218`

3. **WiFi 重连机制去阻塞**
   - 问题：事件回调阻塞。
   - 建议：重连退避迁移到专用任务/定时器，事件回调只做派发。
   - 预期收益：提升断网恢复稳定性。
   - 成本：中
   - 涉及文件：`main/wifi/wifi_manager.c:209`、`main/wifi/wifi_manager.c:223`

4. **发布链路与文档对齐**
   - 问题：发布产物和命令说明偏差。
   - 建议：补齐 SPIFFS 产物策略；修正文档中的旧 CLI 命令；明确 OTA 路径。
   - 预期收益：减少现场交付错误。
   - 成本：低-中
   - 涉及文件：`CMakeLists.txt:9`、`partitions.csv:7`、`.github/workflows/release.yml:32`、`.github/workflows/release.yml:75`、`main/cli/serial_cli.c:105`、`main/ota/ota_manager.c:11`

### P1（1个月内）

1. **测试门禁建设**：先补纯 C 单测（session/tool/cron），CI 增加最小门禁。  
2. **会话存储治理**：滚动归档、动态行读取、解析失败显式告警。  
3. **可观测性增强**：队列丢弃、LLM 延迟、工具失败率等指标 + `health` 命令。  
4. **文档一致性机制**：配置方式、命令集、能力矩阵纳入 PR 检查。

### P2（演进优化）

1. **大文件拆分与领域解耦**：拆分 `serial_cli` 与 `llm_proxy`。  
2. **Prompt 构建缓存**：稳定段缓存 + 增量失效。  
3. **能力状态分层**：明确 `ui` / `ota` 为启用、实验或废弃。

## 快速收益清单（1天内可完成）

- 移除默认硬编码 API Key，并在启动时校验缺省配置。  
- 修复 Release 文档里的旧 CLI 命令（`wifi/tg_token/api_key` → `set_*`）。  
- 修正文档中过时描述（运行时配置、cron/heartbeat/skills 状态）。  
- 修正 prompt 中 daily 路径与真实存储路径不一致。  
- 为队列丢弃增加基础统计输出（CLI 可查）。

## 分阶段路线图

- **1周**：完成 P0（安全基线、web_search、WiFi 重连、发布对齐）并补最小回归脚手架。  
- **1月**：完成 P1（测试门禁、会话治理、可观测性、文档一致性）并启动 P2 拆分。

## 需要确认的决策点

1. 产品定位是“内网玩具”还是“可公开部署”？（决定鉴权强度）
2. `web_search` 对接官方 API 还是私有网关？
3. OTA 主路径采用 CLI、工具触发，还是独立 endpoint？
4. 测试投入先做核心 20% 覆盖，还是直接上端到端硬件回归？

## 结论摘要

- 架构主干已经成型，功能覆盖较完整。
- 当前最关键短板是：安全基线、发布一致性、测试缺失。
- `web_search` 是最紧急且可见的修复点。
- 建议先做 1 周 P0 收敛，再进入 1 个月稳定化建设。
