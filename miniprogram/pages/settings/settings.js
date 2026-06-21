const ble = require('../../utils/ble');

Page({
  data: {
    state: ble.getState(),
    logs: ble.getLogs(),
    busy: false,
    tipVisible: false,
    tipTitle: '',
    tipText: ''
  },

  onShow() {
    this.unsubscribers = [
      ble.on('state', (state) => this.setData({ state })),
      ble.on('log', (logs) => this.setData({ logs })),
      ble.on('error', (message) => this.toast(message, 'none'))
    ];

    this.setData({
      state: ble.getState(),
      logs: ble.getLogs()
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

  async pingEsp32() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接仪表', 'none');
      return;
    }

    await this.runTask(async () => {
      await ble.sendCommand('PING');
      this.toast('已发送 PING', 'success');
    });
  },

  async rebootEsp32() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接仪表', 'none');
      return;
    }

    wx.showModal({
      title: '重启 ESP32',
      content: '重启后手机会与仪表断开，需要重新搜索连接。',
      confirmText: '重启',
      confirmColor: '#ffbf47',
      success: async (res) => {
        if (!res.confirm) {
          return;
        }

        await this.runTask(async () => {
          await ble.reboot();
          this.toast('已发送重启命令', 'success');
        });
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
      this.toast(message, 'none');
    } finally {
      this.setData({ busy: false });
    }
  },

  showTip(event) {
    this.setData({
      tipVisible: true,
      tipTitle: event.currentTarget.dataset.title || '说明',
      tipText: event.currentTarget.dataset.text || ''
    });
  },

  hideTip() {
    this.setData({
      tipVisible: false,
      tipTitle: '',
      tipText: ''
    });
  },

  toast(title, icon) {
    wx.showToast({
      title: String(title || '').slice(0, 30),
      icon: icon || 'none'
    });
  }
});
