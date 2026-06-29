const api = require('../../services/api');

Page({
  data: {
    state: api.getState(),
    settings: api.getSettings(),
    busy: false
  },

  onShow() {
    this.unsubscribers = [
      api.on('state', (state) => this.setData({ state })),
      api.on('error', (message) => this.toast(message))
    ];

    this.setData({
      state: api.getState(),
      settings: api.getSettings()
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

  rebootInstrument() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接X仪表');
      return;
    }

    wx.showModal({
      title: '重启仪表',
      content: 'X仪表会立即重启，稍后可重新连接。',
      confirmText: '重启',
      confirmColor: '#25f0b0',
      success: (res) => {
        if (!res.confirm) {
          return;
        }

        this.runTask(async () => {
          await api.reboot();
          this.toast('已发送重启指令', 'success');
        });
      }
    });
  },

  restoreDefaults() {
    wx.showModal({
      title: '恢复默认设置',
      content: '这会恢复本页设置，不会清除已保存的电池。',
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

  async runTask(task) {
    if (this.data.busy) {
      return;
    }

    this.setData({ busy: true });
    try {
      await task();
    } catch (error) {
      const message = error && (error.errMsg || error.message) ? (error.errMsg || error.message) : '操作失败';
      this.toast(message);
    } finally {
      this.setData({ busy: false });
    }
  },

  toast(title, icon) {
    wx.showToast({
      title: String(title || '').slice(0, 30),
      icon: icon || 'none'
    });
  }
});
