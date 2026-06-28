const api = require('../../services/api');

Page({
  data: {
    view: api.getInstrumentView(),
    busy: false
  },

  onShow() {
    this.unsubscribers = [
      api.on('state', (state) => this.updateState(state)),
      api.on('error', (message) => this.toast(message))
    ];

    this.updateState(api.getState());
    if (api.getState().dashboardConnected) {
      api.refreshStatus().catch(() => null);
    }
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
    this.setData({
      view: api.getInstrumentView(state)
    });
  },

  async handlePrimaryAction() {
    const type = this.data.view.primaryType;

    if (type === 'choose-bms') {
      wx.switchTab({ url: '/pages/bms/bms' });
      return;
    }

    if (type !== 'connect-instrument') {
      return;
    }

    await this.runTask(async () => {
      await api.connectInstrument();
      this.toast('X仪表已连接', 'success');
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
