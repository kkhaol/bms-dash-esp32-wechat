const ble = require('../../utils/ble');

Page({
  data: {
    state: ble.getState(),
    bmsList: ble.getBmsList(),
    bmsHistory: ble.getBmsHistory(),
    logs: ble.getLogs(),
    busy: false,
    tipVisible: false,
    tipTitle: '',
    tipText: ''
  },

  onShow() {
    this.unsubscribers = [
      ble.on('state', (state) => this.setData({ state })),
      ble.on('bms', (bmsList) => this.setData({ bmsList })),
      ble.on('history', (bmsHistory) => this.setData({ bmsHistory })),
      ble.on('log', (logs) => this.setData({ logs })),
      ble.on('message', (message) => this.handleMessage(message)),
      ble.on('error', (message) => this.toast(message, 'none'))
    ];

    this.setData({
      state: ble.getState(),
      bmsList: ble.getBmsList(),
      bmsHistory: ble.getBmsHistory(),
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

  handleMessage(message) {
    if (message.type === 'select_ok') {
      this.toast('BMS 已保存并连接', 'success');
    }
  },

  async scanBms() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接仪表', 'none');
      return;
    }

    await this.runTask(async () => {
      await ble.scanBms();
      this.toast('已请求 ESP32 扫描 BMS', 'success');
    });
  },

  async selectBms(event) {
    const mac = event.currentTarget.dataset.mac;
    await this.tryConnectBms(mac);
  },

  async selectHistoryBms(event) {
    const mac = event.currentTarget.dataset.mac;
    await this.tryConnectBms(mac);
  },

  async tryConnectBms(mac) {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接仪表', 'none');
      return;
    }

    await this.runTask(async () => {
      await ble.selectBms(mac);
      this.toast('已请求连接指定 BMS', 'success');
    });
  },

  async reconnectBms() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接仪表', 'none');
      return;
    }

    await this.runTask(async () => {
      await ble.reconnectBms();
      this.toast('已请求重连保存的 BMS', 'success');
    });
  },

  async forgetBms() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接仪表', 'none');
      return;
    }

    wx.showModal({
      title: '清除已保存 BMS',
      content: 'ESP32 将忘记当前保存的 BMS。下次开机不会自动连接 BMS。',
      confirmText: '清除',
      confirmColor: '#d84d4d',
      success: async (res) => {
        if (!res.confirm) {
          return;
        }

        await this.runTask(async () => {
          await ble.forgetBms();
          this.toast('已清除保存配置', 'success');
        });
      }
    });
  },

  async refreshStatus() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接仪表', 'none');
      return;
    }

    await this.runTask(async () => {
      await ble.refreshStatus();
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
