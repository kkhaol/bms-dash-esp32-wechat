const api = require('../../services/api');

Page({
  data: {
    state: api.getState(),
    settings: api.getSettings(),
    logs: api.getLogs(),
    busy: false
  },

  onShow() {
    this.unsubscribers = [
      api.on('state', (state) => this.setData({ state })),
      api.on('log', (logs) => this.setData({ logs })),
      api.on('error', (message) => this.toast(message))
    ];

    this.setData({
      state: api.getState(),
      settings: api.getSettings(),
      logs: api.getLogs()
    });
  },

  onHide() {
    this.cleanupListeners();
  },

  onUnload() {
    this.cleanupListeners();
  },

  cleanupListeners() {
    (this.unsubscribers || []).forEach((unsubscribe) => unsubscribe());
    this.unsubscribers = [];
  },

  onSwitchChange(event) {
    const key = event.currentTarget.dataset.key;
    this.setData({
      settings: api.updateSetting(key, !!event.detail.value)
    });
  },

  onBrightnessChange(event) {
    this.setData({
      settings: api.updateSetting('brightness', event.detail.value)
    });
  },

  onLowBatteryChange(event) {
    this.setData({
      settings: api.updateSetting('lowBatteryThreshold', event.detail.value)
    });
  },

  restoreDefaults() {
    wx.showModal({
      title: '恢复默认设置',
      content: '这会恢复本页的高级设置，不会清除已保存的 BMS。',
      confirmText: '恢复',
      confirmColor: '#25f0b0',
      success: (res) => {
        if (!res.confirm) {
          return;
        }

        this.setData({
          settings: api.resetSettings()
        });
        this.toast('已恢复默认设置', 'success');
      }
    });
  },

  toast(title, icon) {
    wx.showToast({
      title: String(title || '').slice(0, 30),
      icon: icon || 'none'
    });
  }
});
