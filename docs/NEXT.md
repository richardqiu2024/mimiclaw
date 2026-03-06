# 下一步开发计划

## 混合模式 Agent 架构（方案 C）

**更新时间**: 2026-03-07
**状态**: 设计阶段

---

## 一、核心设计理念

实现本地 ESP32 Agent 与云端 Agent 的智能协同工作：

- **本地优先**：低延迟、隐私保护、离线可用
- **云端补充**：强大推理、复杂任务、多模态能力
- **双向通信**：云端可以回调 ESP32 的硬件资源

---

## 二、架构设计

### 2.1 任务路由策略

#### 本地处理场景
- 简单问答（"现在几点？"）
- 硬件控制（GPIO、传感器）
- 文件操作
- 定时任务管理
- 快速响应需求（<2秒）

#### 云端委托场景
- 代码生成/调试
- 复杂推理（多步骤逻辑）
- 长文本分析（>500字）
- 多模态任务（图像、语音）
- 需要最新知识

#### 路由判断逻辑

**方法 1：关键词匹配**（Phase 1 实现）
```
触发词：["代码", "分析", "生成", "复杂", "详细", "解释"]
消息长度：>200 字符
用户显式指定：@cloud / @local
```

**方法 2：LLM 预判**（Phase 3 优化）
```
本地 LLM 快速判断任务复杂度
```

---

## 三、通信协议

### 3.1 ESP32 → 云端

**请求**：
```json
{
  "task_id": "uuid-1234",
  "prompt": "用户问题",
  "context": {
    "conversation_history": [...],
    "esp32_capabilities": ["gpio", "sensor", "file"],
    "callback_url": "http://esp32-ip:18789/tool/callback"
  },
  "timeout": 30000
}
```

**响应**：
```json
{
  "task_id": "uuid-1234",
  "result": "云端回复",
  "tool_calls": [...],
  "usage": {"tokens": 1234}
}
```

### 3.2 云端 → ESP32（回调）

**请求**：
```json
{
  "task_id": "uuid-1234",
  "tool": "read_sensor",
  "params": {"sensor_id": "temp1"}
}
```

**响应**：
```json
{
  "success": true,
  "result": "Temperature: 25.3°C"
}
```

---

## 四、ESP32 端实现

### 4.1 新增组件

1. **任务路由器** (`agent/task_router.c`)
   - 分析任务复杂度
   - 决定本地/云端执行

2. **Cloud Delegate 工具** (`tools/tool_cloud_delegate.c`)
   - HTTP POST 到云端服务
   - 复用 web_search 的 HTTP 客户端逻辑
   - 支持超时和重试

3. **HTTP 回调服务器** (`http/callback_server.c`)
   - 接收云端工具调用请求
   - 基于现有 WebSocket gateway 扩展
   - 端口：18789

4. **配置管理**
   - NVS 存储云端 URL
   - CLI 命令：`set_cloud_url`

### 4.2 配置项

在 `mimi_config.h` 添加：
```c
#define MIMI_CLOUD_AGENT_URL     "http://192.168.1.175:8000"
#define MIMI_CLOUD_TIMEOUT_MS    30000
#define MIMI_CLOUD_ENABLED       1
#define MIMI_TASK_ROUTE_AUTO     1
```

---

## 五、云端服务实现

### 5.1 技术栈

- **框架**：FastAPI (Python)
- **LLM SDK**：Anthropic SDK (Claude)
- **部署**：Docker + Uvicorn
- **可选**：Redis（任务队列）

### 5.2 服务架构

```
cloud_agent/
├── main.py                 # FastAPI 入口
├── agent/
│   ├── claude_client.py    # Claude API 封装
│   ├── tools.py            # 工具定义
│   └── executor.py         # 任务执行器
├── esp32/
│   └── callback.py         # ESP32 回调客户端
├── config.py
├── requirements.txt
└── Dockerfile
```

### 5.3 核心工具

- `call_esp32_tool`：回调 ESP32 硬件
- `web_search`：云端搜索
- `code_execution`：代码沙箱
- `image_analysis`：图像处理

---

## 六、工作流程示例

### 场景 1：简单查询（本地）
```
用户: "现在几点？"
  ↓
任务路由器: LOCAL
  ↓
本地 Agent: get_current_time
  ↓
返回: "2026-03-07 16:30"
```

### 场景 2：复杂任务（云端）
```
用户: "写一个 Python 爬虫"
  ↓
任务路由器: CLOUD（检测到"写"、"Python"）
  ↓
cloud_delegate 工具
  ↓
云端 Claude: 生成代码
  ↓
返回: 完整代码 + 说明
```

### 场景 3：协同工作
```
用户: "分析传感器数据趋势"
  ↓
委托到云端
  ↓
云端: 需要数据 → call_esp32_tool
  ↓
ESP32: 执行 read_sensor，返回数据
  ↓
云端: 分析并生成报告
  ↓
返回: "温度平均25°C，趋势稳定"
```

---

## 七、实现优先级

### Phase 1：基础功能（1-2天）
- [ ] ESP32 添加 `cloud_delegate` 工具
- [ ] 云端基础 Agent 服务（FastAPI + Claude）
- [ ] 简单任务路由（关键词匹配）

### Phase 2：双向通信（2-3天）
- [ ] ESP32 HTTP 回调服务器
- [ ] 云端调用 ESP32 工具
- [ ] 完整工作流测试

### Phase 3：优化增强（1-2天）
- [ ] 智能路由（LLM 预判）
- [ ] 异步任务队列
- [ ] 错误处理和降级

### Phase 4：高级功能（可选）
- [ ] 多模态支持（图像/语音）
- [ ] 分布式部署（多 ESP32）
- [ ] 监控和日志系统

---

## 八、关键技术细节

### 8.1 超时处理
- ESP32 设置 30 秒超时
- 云端返回 202 Accepted + task_id
- 支持轮询或 webhook

### 8.2 错误处理
- 云端不可用 → 自动降级到本地
- 回调失败 → 重试 3 次
- 超时 → 返回错误提示

### 8.3 安全性
- ESP32 → 云端：API key 认证
- 云端 → ESP32：预共享密钥
- 支持 HTTPS（云端）
- 内网隔离（ESP32 callback）

### 8.4 成本控制
- 简单任务本地处理（免费）
- 云端任务计费（Claude API）
- 设置每日 token 上限
- 缓存常见问题

---

## 九、配置和部署

### ESP32 配置
```bash
# 串口 CLI
> set_cloud_url http://192.168.1.175:8000
> set_cloud_key your-api-key
> set_task_route auto  # auto / local / cloud
```

### 云端部署
```bash
# Docker 一键部署
docker run -d \
  -p 8000:8000 \
  -e ANTHROPIC_API_KEY=your-key \
  -e ESP32_CALLBACK_SECRET=shared-secret \
  cloud-agent:latest
```

---

## 十、测试计划

### 单元测试
- ESP32 工具独立测试
- 云端 API 端点测试

### 集成测试
- 本地任务路由
- 云端委托流程
- 回调机制

### 性能测试
- 延迟对比（本地 vs 云端）
- 并发测试
- 稳定性测试（长时间运行）

---

## 十一、预期收益

### 功能增强
- 支持复杂任务（代码生成、深度分析）
- 多模态能力（图像、语音）
- 更强的推理能力

### 用户体验
- 简单任务快速响应（本地）
- 复杂任务高质量输出（云端）
- 无缝切换，用户无感知

### 成本优化
- 简单任务不消耗 API token
- 按需使用云端资源
- 可控的成本预算

---

## 相关文档

- [架构文档](ARCHITECTURE.md)
- [TODO 列表](TODO.md)
- [Web Search 实现](../main/tools/tool_web_search.c)

---

**下一步行动**：开始实现 Phase 1 - 基础功能
