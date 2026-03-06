const { encodeUtf8, decodeUtf8 } = require("../../utils/utf8");

const SERVICE_UUID = "6e400001-b5a3-f393-e0a9-500e24dcca9e";
const RX_UUID = "6e400002-b5a3-f393-e0a9-500e24dcca9e";
const TX_UUID = "6e400003-b5a3-f393-e0a9-500e24dcca9e";
const DEVICE_NAME = "MimiClaw-CLI";
const CHUNK_SIZE = 20;
const PROMPT_TOKEN = "mimi> ";
const STORAGE_KEY_LAST_DEVICE = "mimiclaw.lastDeviceId";
const WRITE_GAP_MS = 15;
const COMMAND_TIMEOUT_MS = 4000;
const MAX_LOG_LINES = 300;

Page({
  data: {
    state: "DISCONNECTED",
    statusText: "未连接",
    devices: [],
    isConnected: false,
    isReady: false,
    isDiscovering: false,
    inputCommand: "",
    logs: [],
    partialLine: "",
    logBottomId: "logBottom-0",
    lastDeviceId: "",
    showAllDevices: false,
    commandQueueLength: 0,
    wifiSsid: "",
    wifiPassword: "",
    wifiStaticIp: "",
    wifiStaticMask: "",
    wifiStaticGateway: "",
    wifiStaticDns1: "",
    wifiStaticDns2: "",
    apiKey: "",
    tgToken: "",
    model: "",
    configProviderOptions: ["anthropic", "openai"],
    configProviderIndex: 0,
  },

  onLoad() {
    this.deviceMap = new Map();
    this.commandQueue = [];
    this.isSending = false;
    this.rxBuffer = "";
    this.commandTimer = null;
    this.discoveryTimer = null;
    this.currentCommand = null;
    this.serviceId = "";
    this.rxCharId = "";
    this.txCharId = "";
    this.logSeq = 0;

    this.onDeviceFoundHandler = this.handleDeviceFound.bind(this);
    this.onConnectionStateHandler = this.handleConnectionState.bind(this);
    this.onNotifyHandler = this.handleNotify.bind(this);
    this.onAdapterStateHandler = (res) => {
      this.logSystem(`蓝牙状态: available=${res.available}, discovering=${res.discovering}`);
      if (!res.available) {
        this.setState("DISCONNECTED", "蓝牙不可用");
      }
    };

    wx.onBluetoothDeviceFound(this.onDeviceFoundHandler);
    wx.onBLEConnectionStateChange(this.onConnectionStateHandler);
    wx.onBLECharacteristicValueChange(this.onNotifyHandler);
    wx.onBluetoothAdapterStateChange(this.onAdapterStateHandler);

    const lastDeviceId = wx.getStorageSync(STORAGE_KEY_LAST_DEVICE) || "";
    if (lastDeviceId) {
      this.setData({ lastDeviceId });
    }

    wx.getSystemInfo({
      success: (info) => {
        this.logSystem(`系统: ${info.platform} ${info.system} | 微信 ${info.version} | 基础库 ${info.SDKVersion}`);
      },
    });
  },

  onHide() {
    this.stopDiscovery();
  },

  onUnload() {
    wx.offBluetoothDeviceFound(this.onDeviceFoundHandler);
    wx.offBLEConnectionStateChange(this.onConnectionStateHandler);
    wx.offBLECharacteristicValueChange(this.onNotifyHandler);
    wx.offBluetoothAdapterStateChange(this.onAdapterStateHandler);
    this.clearCommandTimer();
    clearTimeout(this.discoveryTimer);
    this.stopDiscovery();
  },

  setState(state, statusText) {
    const patch = { state, statusText };
    if (state === "DISCONNECTED") {
      patch.isConnected = false;
      patch.isReady = false;
      patch.isDiscovering = false;
    } else if (state === "SCANNING") {
      patch.isReady = false;
      patch.isDiscovering = true;
    } else if (state === "READY") {
      patch.isConnected = true;
      patch.isReady = true;
      patch.isDiscovering = false;
    } else {
      patch.isReady = false;
    }
    this.setData(patch);
  },

  onScan() {
    if (this.data.isDiscovering) {
      this.stopDiscovery();
      this.setState("DISCONNECTED", "扫描已停止");
      return;
    }

    this.logSystem("准备扫描设备...");
    this.ensurePermissions()
      .then(() => this.openAdapter())
      .then(() => this.startDiscovery())
      .catch((err) => this.logSystem(`扫描失败: ${err}`));
  },

  onRequestPermissions() {
    this.ensurePermissions(true).catch((err) => this.logSystem(`权限请求失败: ${err}`));
  },

  onToggleShowAll() {
    const next = !this.data.showAllDevices;
    this.setData({ showAllDevices: next });
    this.logSystem(next ? "调试：显示所有设备" : "仅显示 MimiClaw-CLI 设备");
    this.refreshDeviceList();
  },

  onReconnect() {
    const deviceId = this.data.lastDeviceId || wx.getStorageSync(STORAGE_KEY_LAST_DEVICE);
    if (!deviceId) {
      this.logSystem("无上次设备记录");
      return;
    }
    this.connectDevice(deviceId);
  },

  onConnect(e) {
    const deviceId = e.currentTarget.dataset.deviceid;
    this.connectDevice(deviceId);
  },

  onDisconnect() {
    if (!this.data.isConnected) return;
    wx.closeBLEConnection({
      deviceId: this.data.lastDeviceId,
      complete: () => {
        this.setState("DISCONNECTED", "已断开");
      },
    });
  },

  onInput(e) {
    this.setData({ inputCommand: e.detail.value });
  },

  onSend() {
    const cmd = this.data.inputCommand.trim();
    if (!cmd) return;
    this.enqueueCommand(cmd);
    this.setData({ inputCommand: "" });
  },

  onQuickCommand(e) {
    const cmd = e.currentTarget.dataset.cmd;
    if (!cmd) return;
    this.enqueueCommand(cmd);
  },

  onClearLogs() {
    this.logSeq += 1;
    this.setData({
      logs: [],
      partialLine: "",
      logBottomId: `logBottom-${this.logSeq}`,
    });
    this.logSystem("日志已清空");
  },

  onConfigInput(e) {
    const field = e.currentTarget.dataset.field;
    if (!field) return;
    this.setData({ [field]: e.detail.value });
  },

  onProviderChange(e) {
    const index = parseInt(e.detail.value, 10);
    if (isNaN(index)) return;
    this.setData({ configProviderIndex: index });
  },

  onSetWifi() {
    const ssid = this.data.wifiSsid.trim();
    const password = this.data.wifiPassword.trim();
    if (!ssid || !password) {
      this.logSystem("请先填写 WiFi SSID 和密码");
      return;
    }
    this.enqueueCommand(`set_wifi ${this.escapeCliArg(ssid)} ${this.escapeCliArg(password)}`);
  },

  onSetApiKey() {
    const apiKey = this.data.apiKey.trim();
    if (!apiKey) {
      this.logSystem("请先填写 API Key");
      return;
    }
    this.enqueueCommand(`set_api_key ${this.escapeCliArg(apiKey)}`);
  },

  onSetWifiStatic() {
    const ip = this.data.wifiStaticIp.trim();
    const mask = this.data.wifiStaticMask.trim();
    const gateway = this.data.wifiStaticGateway.trim();
    const dns1 = this.data.wifiStaticDns1.trim();
    const dns2 = this.data.wifiStaticDns2.trim();

    if (!ip || !mask || !gateway) {
      this.logSystem("请先填写静态 IP、子网掩码、网关");
      return;
    }

    if (!this.isValidIPv4(ip) || !this.isValidIPv4(mask) || !this.isValidIPv4(gateway)) {
      this.logSystem("静态 IP/掩码/网关格式不正确，请输入 IPv4 地址");
      return;
    }

    if (dns1 && !this.isValidIPv4(dns1)) {
      this.logSystem("DNS1 格式不正确，请输入 IPv4 地址");
      return;
    }

    if (dns2 && !this.isValidIPv4(dns2)) {
      this.logSystem("DNS2 格式不正确，请输入 IPv4 地址");
      return;
    }

    if (dns2 && !dns1) {
      this.logSystem("如需填写 DNS2，请先填写 DNS1");
      return;
    }

    const args = [ip, mask, gateway];
    if (dns1) {
      args.push(dns1);
    }
    if (dns2) {
      args.push(dns2);
    }

    const cmd = `set_wifi_static ${args.map((item) => this.escapeCliArg(item)).join(" ")}`;
    this.enqueueCommand(cmd);
  },

  onClearWifiStatic() {
    this.enqueueCommand("clear_wifi_static");
  },

  onSetTgToken() {
    const token = this.data.tgToken.trim();
    if (!token) {
      this.logSystem("请先填写 Telegram Token");
      return;
    }
    this.enqueueCommand(`set_tg_token ${this.escapeCliArg(token)}`);
  },

  onSetModel() {
    const model = this.data.model.trim();
    if (!model) {
      this.logSystem("请先填写模型名");
      return;
    }
    this.enqueueCommand(`set_model ${this.escapeCliArg(model)}`);
  },

  onSetProvider() {
    const provider = this.data.configProviderOptions[this.data.configProviderIndex];
    if (!provider) return;
    this.enqueueCommand(`set_model_provider ${provider}`);
  },

  onConfigShow() {
    this.enqueueCommand("config_show");
  },

  onConfigReset() {
    wx.showModal({
      title: "确认重置",
      content: "将清除 NVS 中的配置并恢复编译时默认值，是否继续？",
      confirmColor: "#dc2626",
      success: (res) => {
        if (!res.confirm) return;
        this.enqueueCommand("config_reset");
      },
    });
  },

  onRestart() {
    this.enqueueCommand("restart");
  },

  escapeCliArg(raw) {
    const value = String(raw == null ? "" : raw).trim();
    if (!value) return '""';
    if (!/[\s"\\]/.test(value)) return value;
    const escaped = value.replace(/\\/g, "\\\\").replace(/"/g, '\\"');
    return `"${escaped}"`;
  },

  isValidIPv4(value) {
    const parts = String(value || "").split(".");
    if (parts.length !== 4) return false;
    return parts.every((part) => {
      if (!/^\d+$/.test(part)) return false;
      const num = Number(part);
      return num >= 0 && num <= 255;
    });
  },

  openAdapter() {
    return new Promise((resolve, reject) => {
      wx.openBluetoothAdapter({
        mode: "central",
        success: () => {
          wx.getBluetoothAdapterState({
            success: (state) => {
              this.logSystem(`适配器状态: available=${state.available}, discovering=${state.discovering}`);
              if (!state.available) {
                this.logSystem("蓝牙不可用，请开启手机蓝牙后重试");
              }
              resolve();
            },
            fail: () => resolve(),
          });
        },
        fail: (err) => reject(err.errMsg || "openBluetoothAdapter error"),
      });
    });
  },

  ensurePermissions(force) {
    return new Promise((resolve, reject) => {
      wx.getSetting({
        success: (res) => {
          const auth = res.authSetting || {};
          const tasks = [];
          if (force || !auth["scope.bluetooth"]) {
            tasks.push(this.authorizeScope("scope.bluetooth", "蓝牙"));
          }
          if (force || !auth["scope.userLocation"]) {
            tasks.push(this.authorizeScope("scope.userLocation", "定位"));
          }

          Promise.allSettled(tasks)
            .then((results) => {
              results.forEach((result) => {
                if (result.status === "rejected") {
                  this.logSystem(result.reason);
                }
              });
              resolve();
            })
            .catch((err) => reject(err));
        },
        fail: () => resolve(),
      });
    });
  },

  authorizeScope(scope, label) {
    return new Promise((resolve, reject) => {
      wx.authorize({
        scope,
        success: () => {
          this.logSystem(`${label}权限已授权`);
          resolve();
        },
        fail: (err) => {
          const message = `${label}权限未授权，请在系统设置中开启 (${err.errMsg || "unknown"})`;
          reject(message);
        },
      });
    });
  },

  startDiscovery() {
    this.setState("SCANNING", "扫描中...");
    this.deviceMap.clear();
    this.setData({ devices: [] });

    return new Promise((resolve, reject) => {
      wx.startBluetoothDevicesDiscovery({
        allowDuplicatesKey: false,
        powerLevel: "high",
        success: () => {
          this.logSystem("扫描已开始，等待发现设备...");
          this.scheduleDiscoverySnapshot();
          resolve();
        },
        fail: (err) => reject(err.errMsg || "startDiscovery error"),
      });
    });
  },

  scheduleDiscoverySnapshot() {
    clearTimeout(this.discoveryTimer);
    this.discoveryTimer = setTimeout(() => {
      wx.getBluetoothDevices({
        success: (res) => {
          this.logSystem(`扫描快照：共发现 ${res.devices.length} 台设备`);
          res.devices.forEach((device) => this.upsertDevice(device, true));
          this.refreshDeviceList();
        },
        fail: (err) => this.logSystem(`设备快照失败: ${err.errMsg || "unknown"}`),
      });
    }, 2000);
  },

  stopDiscovery() {
    wx.stopBluetoothDevicesDiscovery({
      complete: () => {
        if (this.data.isDiscovering) {
          this.setData({ isDiscovering: false });
        }
      },
    });
  },

  handleDeviceFound(res) {
    const devices = res.devices || [];
    devices.forEach((device) => this.upsertDevice(device, false));
    this.refreshDeviceList();
  },

  upsertDevice(device, fromSnapshot) {
    if (!device.deviceId) return;
    const name = device.name || device.localName || "";
    const rssiText = typeof device.RSSI === "number" ? device.RSSI : "?";
    this.logSystem(`发现设备${fromSnapshot ? "(快照)" : ""}: ${name || "(空)"} | ${device.deviceId} | RSSI=${rssiText}`);

    this.deviceMap.set(device.deviceId, {
      deviceId: device.deviceId,
      name,
      RSSI: device.RSSI,
    });
  },

  refreshDeviceList() {
    const allDevices = Array.from(this.deviceMap.values());
    allDevices.sort((a, b) => {
      const left = typeof a.RSSI === "number" ? a.RSSI : -999;
      const right = typeof b.RSSI === "number" ? b.RSSI : -999;
      return right - left;
    });

    const filtered = this.data.showAllDevices
      ? allDevices
      : allDevices.filter((device) => (device.name || "").includes(DEVICE_NAME));
    this.setData({ devices: filtered });
  },

  connectDevice(deviceId) {
    if (!deviceId) return;
    this.stopDiscovery();
    this.clearCommandTimer();
    this.rxBuffer = "";
    this.commandQueue = [];
    this.setData({
      commandQueueLength: 0,
      partialLine: "",
    });
    this.setState("CONNECTING", "连接中...");

    wx.createBLEConnection({
      deviceId,
      timeout: 10000,
      success: () => {
        this.setData({
          isConnected: true,
          lastDeviceId: deviceId,
        });
        wx.setStorageSync(STORAGE_KEY_LAST_DEVICE, deviceId);
        this.discoverServices(deviceId);
      },
      fail: (err) => {
        this.setState("DISCONNECTED", "连接失败");
        this.logSystem(`连接失败: ${err.errMsg || "unknown"}`);
      },
    });
  },

  discoverServices(deviceId) {
    this.setState("DISCOVERING", "发现服务...");
    wx.getBLEDeviceServices({
      deviceId,
      success: (res) => {
        const services = res.services || [];
        if (services.length === 0) {
          this.logSystem("未找到可用服务");
          this.setState("DISCONNECTED", "未找到服务");
          return;
        }

        const target = services.find((svc) => svc.uuid.toLowerCase() === SERVICE_UUID);
        const serviceIds = target
          ? [target.uuid].concat(services.filter((svc) => svc.uuid !== target.uuid).map((svc) => svc.uuid))
          : services.map((svc) => svc.uuid);
        this.discoverCharacteristicsByService(deviceId, serviceIds, 0);
      },
      fail: (err) => {
        this.setState("DISCONNECTED", "服务发现失败");
        this.logSystem(`服务发现失败: ${err.errMsg || "unknown"}`);
      },
    });
  },

  discoverCharacteristicsByService(deviceId, serviceIds, index) {
    if (index >= serviceIds.length) {
      this.logSystem("未找到 RX/TX 特征");
      this.setState("DISCONNECTED", "特征发现失败");
      return;
    }

    const serviceId = serviceIds[index];
    wx.getBLEDeviceCharacteristics({
      deviceId,
      serviceId,
      success: (res) => {
        const chars = res.characteristics || [];
        const rx = chars.find((item) => item.uuid.toLowerCase() === RX_UUID);
        const tx = chars.find((item) => item.uuid.toLowerCase() === TX_UUID);
        if (!rx || !tx) {
          this.discoverCharacteristicsByService(deviceId, serviceIds, index + 1);
          return;
        }

        this.serviceId = serviceId;
        this.rxCharId = rx.uuid;
        this.txCharId = tx.uuid;
        this.enableNotify(deviceId, serviceId, this.txCharId);
      },
      fail: () => {
        this.discoverCharacteristicsByService(deviceId, serviceIds, index + 1);
      },
    });
  },

  enableNotify(deviceId, serviceId, charId) {
    this.setState("SUBSCRIBED", "订阅通知...");
    wx.notifyBLECharacteristicValueChange({
      deviceId,
      serviceId,
      characteristicId: charId,
      state: true,
      success: () => {
        this.setState("READY", "已连接，可发送命令");
        this.logSystem("BLE CLI 已准备就绪。输入 help 查看指令。");
      },
      fail: (err) => {
        this.setState("DISCONNECTED", "通知订阅失败");
        this.logSystem(`通知订阅失败: ${err.errMsg || "unknown"}`);
      },
    });
  },

  handleConnectionState(res) {
    if (res.deviceId && this.data.lastDeviceId && res.deviceId !== this.data.lastDeviceId) {
      return;
    }

    if (!res.connected) {
      this.setState("DISCONNECTED", "已断开");
      this.isSending = false;
      this.commandQueue = [];
      this.rxBuffer = "";
      this.clearCommandTimer();
      this.setData({
        commandQueueLength: 0,
        partialLine: "",
      });
      this.logSystem("设备已断开，正在等待重连");
    }
  },

  handleNotify(res) {
    const text = decodeUtf8(res.value);
    if (!text) return;

    this.rxBuffer += text.replace(/\r/g, "");
    const lines = this.rxBuffer.split("\n");
    this.rxBuffer = lines.pop() || "";

    lines.forEach((line) => {
      if (!line) return;
      this.appendLog(line);
      if (line.includes(PROMPT_TOKEN)) {
        this.finishCurrentCommand();
      }
    });

    if (this.rxBuffer.includes(PROMPT_TOKEN)) {
      const parts = this.rxBuffer.split(PROMPT_TOKEN);
      for (let i = 0; i < parts.length - 1; i += 1) {
        const fragment = parts[i].trim();
        if (fragment) {
          this.appendLog(fragment);
        }
        this.appendLog(PROMPT_TOKEN.trim());
        this.finishCurrentCommand();
      }
      this.rxBuffer = parts[parts.length - 1];
    }

    this.setPartialLine(this.rxBuffer);

    if (this.currentCommand) {
      this.scheduleCommandTimeout();
    }
  },

  enqueueCommand(cmd) {
    if (!this.data.isReady) {
      this.logSystem("未连接，无法发送命令");
      return;
    }

    this.commandQueue.push(cmd);
    this.setData({ commandQueueLength: this.commandQueue.length });
    if (!this.isSending) {
      this.sendNextCommand();
    }
  },

  async sendNextCommand() {
    if (this.commandQueue.length === 0) {
      this.isSending = false;
      this.setData({ commandQueueLength: 0 });
      return;
    }
    if (!this.data.isReady) {
      this.logSystem("未连接，无法发送命令");
      this.commandQueue = [];
      this.isSending = false;
      this.setData({ commandQueueLength: 0 });
      return;
    }

    this.isSending = true;
    const cmd = this.commandQueue.shift();
    this.setData({ commandQueueLength: this.commandQueue.length });
    this.currentCommand = cmd;
    this.appendLog(`> ${cmd}`);

    const payload = encodeUtf8(`${cmd}\n`);
    const deviceId = this.data.lastDeviceId;

    for (let offset = 0; offset < payload.length; offset += CHUNK_SIZE) {
      const chunk = payload.slice(offset, offset + CHUNK_SIZE);
      try {
        await this.writeChunk(deviceId, this.serviceId, this.rxCharId, chunk.buffer);
        await this.sleep(WRITE_GAP_MS);
      } catch (err) {
        this.appendLog(`发送失败: ${err}`);
        this.finishCurrentCommand();
        return;
      }
    }

    this.scheduleCommandTimeout(true);
  },

  writeChunk(deviceId, serviceId, charId, value) {
    return new Promise((resolve, reject) => {
      wx.writeBLECharacteristicValue({
        deviceId,
        serviceId,
        characteristicId: charId,
        value,
        success: resolve,
        fail: (err) => reject(err.errMsg || "writeBLECharacteristicValue error"),
      });
    });
  },

  scheduleCommandTimeout(force) {
    if (this.commandTimer && !force) return;
    this.clearCommandTimer();
    this.commandTimer = setTimeout(() => {
      this.appendLog("(等待超时，无更多输出)");
      this.finishCurrentCommand();
    }, COMMAND_TIMEOUT_MS);
  },

  clearCommandTimer() {
    if (this.commandTimer) {
      clearTimeout(this.commandTimer);
      this.commandTimer = null;
    }
  },

  finishCurrentCommand() {
    this.clearCommandTimer();
    this.currentCommand = null;
    if (this.commandQueue.length > 0) {
      this.sendNextCommand();
    } else {
      this.isSending = false;
      this.setData({ commandQueueLength: 0 });
    }
  },

  appendLog(line) {
    const logs = this.data.logs.concat([line]);
    const trimmed = logs.slice(-MAX_LOG_LINES);
    this.logSeq += 1;
    this.setData({
      logs: trimmed,
      partialLine: "",
      logBottomId: `logBottom-${this.logSeq}`,
    });
  },

  setPartialLine(line) {
    const next = line || "";
    if (next === this.data.partialLine) return;
    this.logSeq += 1;
    this.setData({
      partialLine: next,
      logBottomId: `logBottom-${this.logSeq}`,
    });
  },

  logSystem(text) {
    this.appendLog(`[${this.formatNow()}][系统] ${text}`);
  },

  formatNow() {
    const now = new Date();
    const pad = (value) => (value < 10 ? `0${value}` : `${value}`);
    return `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
  },

  sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  },
});
