const DEVICE_PREFIX = 'BMS-DASH';
const SERVICE_UUID = '6E400001-B5A3-F393-E0A9-E50E24DCCA9E';
const RX_UUID = '6E400002-B5A3-F393-E0A9-E50E24DCCA9E';
const TX_UUID = '6E400003-B5A3-F393-E0A9-E50E24DCCA9E';
const MAX_BLE_PACKET = 20;
const HISTORY_KEY = 'bms_dash_history_v1';

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function normalizeUuid(uuid) {
  return String(uuid || '').toUpperCase();
}

function normalizeMac(mac) {
  return String(mac || '').trim().toUpperCase();
}

function getDeviceName(device) {
  return device.name || device.localName || '';
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function callWx(method, params) {
  return new Promise((resolve, reject) => {
    if (!wx || !wx[method]) {
      reject(new Error(`wx.${method} is unavailable`));
      return;
    }

    wx[method]({
      ...(params || {}),
      success: resolve,
      fail: reject
    });
  });
}

function utf8BytesFromString(input) {
  const bytes = [];
  const text = String(input || '');

  for (let i = 0; i < text.length; i += 1) {
    let code = text.charCodeAt(i);

    if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.length) {
      const next = text.charCodeAt(i + 1);
      if (next >= 0xdc00 && next <= 0xdfff) {
        code = 0x10000 + ((code - 0xd800) << 10) + (next - 0xdc00);
        i += 1;
      }
    }

    if (code <= 0x7f) {
      bytes.push(code);
    } else if (code <= 0x7ff) {
      bytes.push(0xc0 | (code >> 6));
      bytes.push(0x80 | (code & 0x3f));
    } else if (code <= 0xffff) {
      bytes.push(0xe0 | (code >> 12));
      bytes.push(0x80 | ((code >> 6) & 0x3f));
      bytes.push(0x80 | (code & 0x3f));
    } else {
      bytes.push(0xf0 | (code >> 18));
      bytes.push(0x80 | ((code >> 12) & 0x3f));
      bytes.push(0x80 | ((code >> 6) & 0x3f));
      bytes.push(0x80 | (code & 0x3f));
    }
  }

  return bytes;
}

function stringFromUtf8Buffer(buffer) {
  const bytes = new Uint8Array(buffer);
  let output = '';
  let i = 0;

  while (i < bytes.length) {
    const first = bytes[i];

    if (first < 0x80) {
      output += String.fromCharCode(first);
      i += 1;
    } else if ((first & 0xe0) === 0xc0 && i + 1 < bytes.length) {
      const code = ((first & 0x1f) << 6) | (bytes[i + 1] & 0x3f);
      output += String.fromCharCode(code);
      i += 2;
    } else if ((first & 0xf0) === 0xe0 && i + 2 < bytes.length) {
      const code = ((first & 0x0f) << 12) |
        ((bytes[i + 1] & 0x3f) << 6) |
        (bytes[i + 2] & 0x3f);
      output += String.fromCharCode(code);
      i += 3;
    } else if ((first & 0xf8) === 0xf0 && i + 3 < bytes.length) {
      let code = ((first & 0x07) << 18) |
        ((bytes[i + 1] & 0x3f) << 12) |
        ((bytes[i + 2] & 0x3f) << 6) |
        (bytes[i + 3] & 0x3f);
      code -= 0x10000;
      output += String.fromCharCode(0xd800 + (code >> 10));
      output += String.fromCharCode(0xdc00 + (code & 0x3ff));
      i += 4;
    } else {
      output += '?';
      i += 1;
    }
  }

  return output;
}

function arrayBufferFromBytes(bytes) {
  const buffer = new ArrayBuffer(bytes.length);
  const view = new Uint8Array(buffer);
  view.set(bytes);
  return buffer;
}

class BleClient {
  constructor() {
    this.listeners = {
      state: [],
      devices: [],
      bms: [],
      history: [],
      message: [],
      log: [],
      error: []
    };

    this.state = {
      adapterReady: false,
      dashboardScanning: false,
      dashboardConnecting: false,
      dashboardConnected: false,
      dashboardDeviceId: '',
      dashboardName: '',
      selectedDashboardDeviceId: '',
      bmsScanning: false,
      bmsConnected: false,
      selectedBmsMac: '',
      selectedBmsName: '',
      voltage: null,
      current: null,
      soc: null,
      temperature: null
    };

    this.dashboardDevices = [];
    this.bmsList = [];
    this.bmsHistory = this.loadHistory();
    this.logs = [];
    this.receiveBuffer = '';
    this.deviceId = '';
    this.serviceId = '';
    this.rxId = '';
    this.txId = '';
    this.listenerInstalled = false;
    this.writeQueue = Promise.resolve();
  }

  on(type, handler) {
    if (!this.listeners[type]) {
      this.listeners[type] = [];
    }

    this.listeners[type].push(handler);
    return () => this.off(type, handler);
  }

  off(type, handler) {
    const list = this.listeners[type] || [];
    this.listeners[type] = list.filter((item) => item !== handler);
  }

  emit(type, payload) {
    const list = this.listeners[type] || [];
    list.forEach((handler) => {
      try {
        handler(payload);
      } catch (error) {
        console.warn('[ble listener error]', error);
      }
    });
  }

  log(message) {
    const date = new Date();
    const time = `${this.pad(date.getHours())}:${this.pad(date.getMinutes())}:${this.pad(date.getSeconds())}`;
    const line = `${time} ${message}`;
    this.logs.unshift(line);
    this.logs = this.logs.slice(0, 100);
    this.emit('log', clone(this.logs));
  }

  pad(value) {
    return value < 10 ? `0${value}` : String(value);
  }

  setState(patch) {
    this.state = {
      ...this.state,
      ...patch
    };
    this.emit('state', clone(this.state));
  }

  getState() {
    return clone(this.state);
  }

  getDashboardDevices() {
    return clone(this.dashboardDevices);
  }

  getBmsList() {
    return clone(this.bmsList);
  }

  getBmsHistory() {
    return clone(this.bmsHistory);
  }

  getLogs() {
    return clone(this.logs);
  }

  loadHistory() {
    try {
      const value = wx.getStorageSync(HISTORY_KEY);
      return Array.isArray(value) ? value : [];
    } catch (error) {
      return [];
    }
  }

  saveHistory() {
    try {
      wx.setStorageSync(HISTORY_KEY, this.bmsHistory);
    } catch (error) {
      this.log('BMS history save failed');
    }
  }

  rememberBmsHistory(bms) {
    const mac = normalizeMac(bms && bms.mac);
    if (!mac) {
      return;
    }

    const item = {
      name: (bms && bms.name) || 'Unknown BMS',
      mac,
      rssi: typeof (bms && bms.rssi) === 'number' ? bms.rssi : -127,
      lastSeenAt: Date.now()
    };

    const index = this.bmsHistory.findIndex((existing) => normalizeMac(existing.mac) === mac);
    if (index >= 0) {
      this.bmsHistory.splice(index, 1);
    }

    this.bmsHistory.unshift(item);
    this.bmsHistory = this.bmsHistory.slice(0, 12);
    this.saveHistory();
    this.emit('history', clone(this.bmsHistory));
  }

  installListeners() {
    if (this.listenerInstalled) {
      return;
    }

    this.listenerInstalled = true;

    wx.onBluetoothAdapterStateChange((res) => {
      this.setState({
        adapterReady: !!res.available,
        dashboardScanning: !!res.discovering
      });

      if (!res.available) {
        this.resetConnectionState('Bluetooth adapter unavailable');
      }
    });

    wx.onBluetoothDeviceFound((res) => {
      const found = res.devices || [];
      this.handleDashboardDevices(found);
    });

    wx.onBLEConnectionStateChange((res) => {
      if (!this.deviceId || res.deviceId !== this.deviceId) {
        return;
      }

      if (!res.connected) {
        this.resetConnectionState('Dashboard disconnected');
      }
    });

    wx.onBLECharacteristicValueChange((res) => {
      if (res.deviceId !== this.deviceId || normalizeUuid(res.characteristicId) !== normalizeUuid(this.txId)) {
        return;
      }

      this.handleNotify(res.value);
    });
  }

  resetConnectionState(reason) {
    this.deviceId = '';
    this.serviceId = '';
    this.rxId = '';
    this.txId = '';
    this.receiveBuffer = '';

    this.setState({
      dashboardConnecting: false,
      dashboardConnected: false,
      dashboardDeviceId: '',
      dashboardName: '',
      bmsScanning: false,
      bmsConnected: false
    });

    if (reason) {
      this.log(reason);
    }
  }

  async initBluetooth() {
    this.installListeners();

    if (this.state.adapterReady) {
      return this.getState();
    }

    try {
      await callWx('openBluetoothAdapter');
      const adapterState = await callWx('getBluetoothAdapterState');
      this.setState({
        adapterReady: !!adapterState.available,
        dashboardScanning: !!adapterState.discovering
      });
      this.log('Bluetooth adapter ready');
      return this.getState();
    } catch (error) {
      const message = this.errorMessage(error, 'Bluetooth adapter open failed');
      this.log(message);
      this.emit('error', '请打开手机蓝牙和权限');
      throw error;
    }
  }

  async startDashboardDiscovery() {
    await this.initBluetooth();
    await this.stopDashboardDiscovery(true);

    this.dashboardDevices = [];
    this.emit('devices', clone(this.dashboardDevices));
    this.setState({
      dashboardScanning: true,
      selectedDashboardDeviceId: ''
    });

    try {
      await callWx('startBluetoothDevicesDiscovery', {
        allowDuplicatesKey: false,
        powerLevel: 'high'
      });
      this.log(`Searching dashboard devices with prefix ${DEVICE_PREFIX}`);
    } catch (error) {
      this.setState({ dashboardScanning: false });
      const message = this.errorMessage(error, 'Dashboard discovery failed');
      this.log(message);
      this.emit('error', '搜索X仪表失败');
      throw error;
    }
  }

  async stopDashboardDiscovery(silent) {
    if (!this.state.dashboardScanning) {
      return;
    }

    try {
      await callWx('stopBluetoothDevicesDiscovery');
      this.setState({ dashboardScanning: false });
      if (!silent) {
        this.log('Dashboard discovery stopped');
      }
    } catch (error) {
      if (!silent) {
        this.log(this.errorMessage(error, 'Stop discovery failed'));
      }
    }
  }

  handleDashboardDevices(devices) {
    let changed = false;

    devices.forEach((device) => {
      const name = getDeviceName(device);
      if (!name || !name.startsWith(DEVICE_PREFIX)) {
        return;
      }

      const index = this.dashboardDevices.findIndex((item) => item.deviceId === device.deviceId);
      const nextDevice = {
        deviceId: device.deviceId,
        name,
        localName: device.localName || '',
        rssi: typeof device.RSSI === 'number' ? device.RSSI : -127
      };

      if (index >= 0) {
        this.dashboardDevices.splice(index, 1, nextDevice);
      } else {
        this.dashboardDevices.push(nextDevice);
      }

      changed = true;
    });

    if (!changed) {
      return;
    }

    this.dashboardDevices.sort((a, b) => b.rssi - a.rssi);

    if (!this.state.selectedDashboardDeviceId && this.dashboardDevices[0]) {
      this.setState({ selectedDashboardDeviceId: this.dashboardDevices[0].deviceId });
    }

    this.emit('devices', clone(this.dashboardDevices));
  }

  selectDashboardDevice(deviceId) {
    this.setState({ selectedDashboardDeviceId: deviceId || '' });
  }

  async connectDashboard(deviceId) {
    await this.initBluetooth();

    const targetDeviceId = deviceId || this.state.selectedDashboardDeviceId || (this.dashboardDevices[0] && this.dashboardDevices[0].deviceId);
    if (!targetDeviceId) {
      const message = '未选择X仪表';
      this.emit('error', 'X仪表连接失败');
      throw new Error(message);
    }

    const device = this.dashboardDevices.find((item) => item.deviceId === targetDeviceId) || {};
    await this.stopDashboardDiscovery(true);

    this.setState({
      dashboardConnecting: true,
      selectedDashboardDeviceId: targetDeviceId
    });

    try {
      if (this.state.dashboardConnected && this.deviceId && this.deviceId !== targetDeviceId) {
        await this.disconnect(true);
      }

      this.log(`Connecting dashboard ${device.name || targetDeviceId}`);
      await callWx('createBLEConnection', {
        deviceId: targetDeviceId,
        timeout: 10000
      });

      this.deviceId = targetDeviceId;
      this.setState({
        dashboardConnected: true,
        dashboardConnecting: false,
        dashboardDeviceId: targetDeviceId,
        dashboardName: device.name || DEVICE_PREFIX
      });

      await this.discoverNordicUart();
      await this.enableNotify();
      this.log('Dashboard connected');

      await this.sendCommand('PING');
      await delay(80);
      await this.sendCommand('GET_STATUS');
      return this.getState();
    } catch (error) {
      this.resetConnectionState('Dashboard connection failed');
      const message = this.errorMessage(error, 'Dashboard connection failed');
      this.log(message);
      this.emit('error', message);
      throw error;
    }
  }

  async discoverNordicUart() {
    const servicesRes = await callWx('getBLEDeviceServices', {
      deviceId: this.deviceId
    });

    const services = servicesRes.services || [];
    const service = services.find((item) => normalizeUuid(item.uuid) === SERVICE_UUID);

    if (!service) {
      throw new Error(`Service not found: ${SERVICE_UUID}`);
    }

    const charsRes = await callWx('getBLEDeviceCharacteristics', {
      deviceId: this.deviceId,
      serviceId: service.uuid
    });

    const characteristics = charsRes.characteristics || [];
    const rx = characteristics.find((item) => normalizeUuid(item.uuid) === RX_UUID);
    const tx = characteristics.find((item) => normalizeUuid(item.uuid) === TX_UUID);

    if (!rx || !tx) {
      throw new Error('RX or TX characteristic not found');
    }

    this.serviceId = service.uuid;
    this.rxId = rx.uuid;
    this.txId = tx.uuid;
    this.log('UART service discovered');
  }

  async enableNotify() {
    await callWx('notifyBLECharacteristicValueChange', {
      state: true,
      deviceId: this.deviceId,
      serviceId: this.serviceId,
      characteristicId: this.txId
    });
    this.log('Notify enabled');
  }

  async sendCommand(command) {
    this.writeQueue = this.writeQueue
      .catch(() => null)
      .then(() => this.writeCommandNow(command));
    return this.writeQueue;
  }

  async writeCommandNow(command) {
    if (!this.state.dashboardConnected || !this.deviceId || !this.serviceId || !this.rxId) {
      const message = 'X仪表未连接';
      this.emit('error', message);
      throw new Error(message);
    }

    const text = String(command || '').endsWith('\n') ? String(command || '') : `${command}\n`;
    const bytes = utf8BytesFromString(text);

    for (let offset = 0; offset < bytes.length; offset += MAX_BLE_PACKET) {
      const chunk = bytes.slice(offset, offset + MAX_BLE_PACKET);
      await callWx('writeBLECharacteristicValue', {
        deviceId: this.deviceId,
        serviceId: this.serviceId,
        characteristicId: this.rxId,
        value: arrayBufferFromBytes(chunk)
      });
      await delay(25);
    }

    this.log(`TX ${String(command || '').trim()}`);
  }

  handleNotify(buffer) {
    const text = stringFromUtf8Buffer(buffer);
    this.receiveBuffer += text;

    if (this.receiveBuffer.length > 8192) {
      this.receiveBuffer = this.receiveBuffer.slice(-4096);
      this.log('RX buffer trimmed');
    }

    let newlineIndex = this.receiveBuffer.indexOf('\n');
    while (newlineIndex >= 0) {
      const line = this.receiveBuffer.slice(0, newlineIndex).replace(/\r$/, '').trim();
      this.receiveBuffer = this.receiveBuffer.slice(newlineIndex + 1);

      if (line) {
        this.parseJsonLine(line);
      }

      newlineIndex = this.receiveBuffer.indexOf('\n');
    }
  }

  parseJsonLine(line) {
    try {
      const message = JSON.parse(line);
      this.log(`RX ${line}`);
      this.applyMessage(message);
      this.emit('message', clone(message));
    } catch (error) {
      const text = `Invalid JSON from dashboard: ${line}`;
      this.log(text);
    }
  }

  applyMessage(message) {
    if (!message || !message.type) {
      return;
    }

    if (message.type === 'status') {
      this.setState({
        dashboardName: message.esp || this.state.dashboardName,
        bmsConnected: !!message.bms_connected,
        selectedBmsMac: message.selected_bms || '',
        selectedBmsName: message.name || '',
        voltage: typeof message.voltage === 'number' ? message.voltage : null,
        current: typeof message.current === 'number' ? message.current : null,
        soc: typeof message.soc === 'number' ? message.soc : null,
        temperature: readTemperature(message)
      });
      if (message.selected_bms) {
        this.rememberBmsHistory({
          name: message.name || 'Saved BMS',
          mac: message.selected_bms,
          rssi: connectedRssiFromList(this.bmsList, message.selected_bms)
        });
      }
      return;
    }

    if (message.type === 'scan_start') {
      this.bmsList = [];
      this.setState({ bmsScanning: true });
      this.emit('bms', clone(this.bmsList));
      return;
    }

    if (message.type === 'bms_found') {
      this.upsertBms(message);
      return;
    }

    if (message.type === 'scan_done') {
      this.setState({ bmsScanning: false });
      return;
    }

    if (message.type === 'select_ok') {
      const mac = message.mac || this.state.selectedBmsMac;
      const known = this.bmsList.find((item) => normalizeMac(item.mac) === normalizeMac(mac));
      this.setState({
        selectedBmsMac: mac,
        selectedBmsName: known ? known.name : this.state.selectedBmsName
      });
      this.rememberBmsHistory({
        name: known ? known.name : (this.state.selectedBmsName || 'Saved BMS'),
        mac,
        rssi: known ? known.rssi : -127
      });
      return;
    }

    if (message.type === 'error') {
      const text = message.message || '仪表返回错误';
      this.log(`Device error: ${text}`);
      this.emit('error', '操作未完成，请稍后重试');
    }
  }

  upsertBms(message) {
    const mac = message.mac || '';
    if (!mac) {
      return;
    }

    const item = {
      name: message.name || 'Unknown BMS',
      mac,
      rssi: typeof message.rssi === 'number' ? message.rssi : -127
    };

    const index = this.bmsList.findIndex((bms) => bms.mac === mac);
    if (index >= 0) {
      this.bmsList.splice(index, 1, item);
    } else {
      this.bmsList.push(item);
    }

    this.bmsList.sort((a, b) => b.rssi - a.rssi);
    this.emit('bms', clone(this.bmsList));

    const historyItem = this.bmsHistory.find((bms) => normalizeMac(bms.mac) === normalizeMac(mac));
    if (historyItem) {
      this.rememberBmsHistory(item);
    }
  }

  async scanBms() {
    return this.sendCommand('SCAN_BMS');
  }

  async selectBms(mac) {
    if (!mac) {
      throw new Error('请选择电池');
    }

    return this.sendCommand(`SELECT_BMS:${mac}`);
  }

  async forgetBms() {
    await this.sendCommand('FORGET_BMS');
    this.setState({
      selectedBmsMac: '',
      selectedBmsName: '',
      bmsConnected: false
    });
  }

  async reconnectBms() {
    return this.sendCommand('RECONNECT_BMS');
  }

  async reboot() {
    return this.sendCommand('REBOOT');
  }

  async refreshStatus() {
    return this.sendCommand('GET_STATUS');
  }

  async disconnect(silent) {
    if (!this.deviceId) {
      this.resetConnectionState('');
      return;
    }

    const oldDeviceId = this.deviceId;
    try {
      await callWx('closeBLEConnection', {
        deviceId: oldDeviceId
      });
    } catch (error) {
      if (!silent) {
        this.log(this.errorMessage(error, 'Close connection failed'));
      }
    } finally {
      this.resetConnectionState(silent ? '' : 'Dashboard disconnected');
    }
  }

  errorMessage(error, fallback) {
    if (!error) {
      return fallback;
    }

    if (error.errMsg) {
      return `${fallback}: ${error.errMsg}`;
    }

    if (error.message) {
      return `${fallback}: ${error.message}`;
    }

    return fallback;
  }
}

const ble = new BleClient();

ble.DEVICE_PREFIX = DEVICE_PREFIX;
ble.SERVICE_UUID = SERVICE_UUID;
ble.RX_UUID = RX_UUID;
ble.TX_UUID = TX_UUID;

module.exports = ble;

function connectedRssiFromList(list, mac) {
  const item = (list || []).find((bms) => normalizeMac(bms.mac) === normalizeMac(mac));
  return item ? item.rssi : -127;
}

function readTemperature(message) {
  const fields = [
    message.temperature,
    message.battery_temperature,
    message.batteryTemperature,
    message.temp,
    message.mos_temperature,
    message.mosTemp
  ];

  for (let i = 0; i < fields.length; i += 1) {
    if (typeof fields[i] === 'number') {
      return fields[i];
    }
  }

  return null;
}
