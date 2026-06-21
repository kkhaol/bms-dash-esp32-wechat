const ble = require('../../utils/ble');

Page({
  data: {
    state: ble.getState(),
    devices: ble.getDashboardDevices(),
    logs: ble.getLogs(),
    socDegree: 0,
    busy: false,
    tipVisible: false,
    tipTitle: '',
    tipText: ''
  },

  onShow() {
    this.unsubscribers = [
      ble.on('state', (state) => this.updateState(state)),
      ble.on('devices', (devices) => this.setData({ devices })),
      ble.on('log', (logs) => this.setData({ logs })),
      ble.on('error', (message) => this.toast(message, 'none'))
    ];

    this.updateState(ble.getState());
    this.setData({
      devices: ble.getDashboardDevices(),
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

  updateState(state) {
    const soc = typeof state.soc === 'number' ? state.soc : null;
    const safeSoc = soc === null ? 0 : Math.max(0, Math.min(100, soc));
    this.setData({
      state,
      socDegree: Math.round(safeSoc * 3.6)
    });
  },

  async searchDashboard() {
    await this.runTask(async () => {
      await ble.startDashboardDiscovery();
      this.toast('正在搜索 BMS-DASH 仪表', 'none');
    });
  },

  selectDashboard(event) {
    const deviceId = event.currentTarget.dataset.id;
    ble.selectDashboardDevice(deviceId);
  },

  async connectDashboard() {
    await this.runTask(async () => {
      await ble.connectDashboard(this.data.state.selectedDashboardDeviceId);
      this.toast('仪表已连接', 'success');
    });
  },

  async refreshStatus() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接仪表', 'none');
      return;
    }

    await this.runTask(async () => {
      await ble.refreshStatus();
      this.toast('已请求状态', 'success');
    });
  },

  async disconnectDashboard() {
    await this.runTask(async () => {
      await ble.disconnect();
      this.toast('已断开', 'success');
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
