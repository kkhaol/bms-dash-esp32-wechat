const ble = require('../utils/ble');

const SETTINGS_KEY = 'x_instrument_settings_v1';

const DEFAULT_SETTINGS = {
  autoLastBms: true,
  preferStrongest: false,
  brightness: 80,
  lowBatteryThreshold: 20,
  debugLogs: false
};

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function numberOrNull(value) {
  return typeof value === 'number' && !Number.isNaN(value) ? value : null;
}

function formatNumber(value, digits) {
  const numeric = numberOrNull(value);
  if (numeric === null) {
    return '--';
  }

  return Number(numeric.toFixed(digits));
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function getBatteryTemperature(state) {
  return numberOrNull(
    state.temperature !== null && state.temperature !== undefined ? state.temperature :
      state.batteryTemperature !== null && state.batteryTemperature !== undefined ? state.batteryTemperature :
        state.temp !== null && state.temp !== undefined ? state.temp :
          state.mosTemp
  );
}

function getInstrumentView(state) {
  const source = state || ble.getState();
  const soc = numberOrNull(source.soc);
  const safeSoc = soc === null ? 0 : clamp(soc, 0, 100);
  const temperature = getBatteryTemperature(source);

  return {
    raw: clone(source),
    dashboardConnected: !!source.dashboardConnected,
    bmsConnected: !!source.bmsConnected,
    bmsName: source.selectedBmsName || '未选择',
    socText: soc === null ? '--' : String(Math.round(safeSoc)),
    socDegree: Math.round(safeSoc * 3.6),
    voltageText: source.voltage === null || source.voltage === undefined ? '--' : `${formatNumber(source.voltage, 1)} V`,
    temperatureText: temperature === null ? '--' : `${formatNumber(temperature, 1)} °C`,
    primaryText: !source.dashboardConnected ? '连接X仪表' : (!source.bmsConnected ? '选择BMS' : ''),
    primaryType: !source.dashboardConnected ? 'connect-instrument' : (!source.bmsConnected ? 'choose-bms' : '')
  };
}

function waitForDashboardDevice(timeoutMs) {
  const existing = ble.getDashboardDevices();
  if (existing.length > 0) {
    return Promise.resolve(existing);
  }

  return new Promise((resolve) => {
    let finished = false;
    let unsubscribe = null;
    const finish = (devices) => {
      if (finished) {
        return;
      }
      finished = true;
      if (unsubscribe) {
        unsubscribe();
      }
      resolve(devices || ble.getDashboardDevices());
    };

    unsubscribe = ble.on('devices', (devices) => {
      if (devices && devices.length > 0) {
        finish(devices);
      }
    });

    setTimeout(() => finish(ble.getDashboardDevices()), timeoutMs || 8000);
  });
}

async function connectInstrument() {
  const current = ble.getState();
  if (current.dashboardConnected) {
    await ble.refreshStatus().catch(() => null);
    return ble.getState();
  }

  await ble.initBluetooth();

  let devices = ble.getDashboardDevices();
  if (devices.length === 0) {
    await ble.startDashboardDiscovery();
    devices = await waitForDashboardDevice(8000);
  }

  if (!devices.length) {
    await ble.stopDashboardDiscovery(true);
    throw new Error('未发现X仪表');
  }

  const target = devices[0];
  ble.selectDashboardDevice(target.deviceId);
  return ble.connectDashboard(target.deviceId);
}

function getSettings() {
  try {
    const saved = wx.getStorageSync(SETTINGS_KEY);
    return {
      ...DEFAULT_SETTINGS,
      ...(saved || {})
    };
  } catch (error) {
    return clone(DEFAULT_SETTINGS);
  }
}

function saveSettings(settings) {
  const next = {
    ...DEFAULT_SETTINGS,
    ...(settings || {})
  };
  try {
    wx.setStorageSync(SETTINGS_KEY, next);
  } catch (error) {
    ble.log('Settings save failed');
  }
  return clone(next);
}

function updateSetting(key, value) {
  const settings = getSettings();
  settings[key] = value;
  return saveSettings(settings);
}

function resetSettings() {
  return saveSettings(DEFAULT_SETTINGS);
}

function getBmsListWithFlags(list, settings) {
  const state = ble.getState();
  const selectedMac = String(state.selectedBmsMac || '').toUpperCase();
  const source = (list || ble.getBmsList()).map((item) => ({
    ...item,
    isLast: selectedMac && String(item.mac || '').toUpperCase() === selectedMac
  }));

  if ((settings || getSettings()).preferStrongest) {
    return source.sort((a, b) => (b.rssi || -127) - (a.rssi || -127));
  }

  return source;
}

function getBmsHistoryWithFlags(list) {
  const state = ble.getState();
  const selectedMac = String(state.selectedBmsMac || '').toUpperCase();
  return (list || ble.getBmsHistory()).map((item) => ({
    ...item,
    isLast: selectedMac && String(item.mac || '').toUpperCase() === selectedMac
  }));
}

module.exports = {
  on: (type, handler) => ble.on(type, handler),
  getState: () => ble.getState(),
  getInstrumentView,
  getBmsList: () => getBmsListWithFlags(),
  getBmsHistory: () => getBmsHistoryWithFlags(),
  getLogs: () => ble.getLogs(),
  getSettings,
  saveSettings,
  updateSetting,
  resetSettings,
  connectInstrument,
  scanBms: () => ble.scanBms(),
  selectBms: (mac) => ble.selectBms(mac),
  reconnectBms: () => ble.reconnectBms(),
  forgetBms: () => ble.forgetBms(),
  refreshStatus: () => ble.refreshStatus(),
  reboot: () => ble.reboot(),
  sendCommand: (command) => ble.sendCommand(command),
  getBmsListWithFlags,
  getBmsHistoryWithFlags
};
