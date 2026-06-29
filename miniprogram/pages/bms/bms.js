const api = require('../../services/api');

function normalizeMac(mac) {
  return String(mac || '').trim().toUpperCase();
}

function signalLabel(rssi) {
  if (typeof rssi !== 'number') {
    return '未知';
  }
  if (rssi > -60) {
    return '强';
  }
  if (rssi > -75) {
    return '良好';
  }
  return '一般';
}

Page({
  data: {
    state: api.getState(),
    settings: api.getSettings(),
    bmsList: [],
    bmsHistory: [],
    selectedMac: '',
    selectedName: '',
    busy: false
  },

  onShow() {
    this.unsubscribers = [
      api.on('state', (state) => this.updateState(state)),
      api.on('bms', (bmsList) => this.updateBmsList(bmsList)),
      api.on('history', (history) => this.updateHistory(history)),
      api.on('message', (message) => this.handleMessage(message)),
      api.on('error', (message) => this.toast(message))
    ];

    const state = api.getState();
    this.setData({
      state,
      settings: api.getSettings(),
      selectedMac: state.selectedBmsMac || ''
    });
    this.refreshLists();
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
    const patch = { state };
    if (!this.data.selectedMac && state.selectedBmsMac) {
      patch.selectedMac = state.selectedBmsMac;
    }
    this.setData(patch);
    this.refreshLists();
  },

  updateBmsList(bmsList) {
    const nextList = this.decorateList(api.getBmsListWithFlags(bmsList, this.data.settings));
    this.setData({ bmsList: nextList });

    if (this.data.settings.preferStrongest && !this.data.selectedMac) {
      const first = nextList[0];
      if (first) {
        this.setSelected(first);
      }
    } else {
      this.updateSelectedName();
    }
  },

  updateHistory(history) {
    this.setData({
      bmsHistory: this.decorateList(api.getBmsHistoryWithFlags(history))
    });
    this.updateSelectedName();
  },

  refreshLists() {
    this.setData({
      bmsList: this.decorateList(api.getBmsList()),
      bmsHistory: this.decorateList(api.getBmsHistory())
    });
    this.updateSelectedName();
  },

  decorateList(list) {
    const selectedMac = normalizeMac(this.data.selectedMac || this.data.state.selectedBmsMac);
    return (list || []).map((item, index) => ({
      ...item,
      displayName: item.isLast ? '上次使用的电池' : `电池 ${index + 1}`,
      selected: selectedMac && normalizeMac(item.mac) === selectedMac,
      signalText: signalLabel(item.rssi)
    }));
  },

  updateSelectedName() {
    const selectedMac = normalizeMac(this.data.selectedMac);
    const all = [...this.data.bmsList, ...this.data.bmsHistory];
    const selected = all.find((item) => normalizeMac(item.mac) === selectedMac);
    this.setData({
      selectedName: selected ? selected.displayName : (this.data.state.selectedBmsName ? '已选择电池' : '')
    });
  },

  setSelected(item) {
    this.setData({
      selectedMac: item.mac,
      selectedName: item.displayName || '电池'
    });
    this.refreshLists();
  },

  chooseBms(event) {
    const mac = event.currentTarget.dataset.mac;
    const all = [...this.data.bmsList, ...this.data.bmsHistory];
    const item = all.find((candidate) => normalizeMac(candidate.mac) === normalizeMac(mac));
    if (item) {
      this.setSelected(item);
    }
  },

  async connectInstrument() {
    await this.runTask(async () => {
      await api.connectInstrument();
      this.toast('X仪表已连接', 'success');
    });
  },

  async scanBms() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接X仪表');
      return;
    }

    await this.runTask(async () => {
      await api.scanBms();
      this.toast('正在搜索附近电池', 'none');
    });
  },

  async connectSelectedBms() {
    if (!this.data.state.dashboardConnected) {
      this.toast('请先连接X仪表');
      return;
    }

    if (!this.data.selectedMac) {
      this.toast('请先选择电池');
      return;
    }

    await this.runTask(async () => {
      await api.selectBms(this.data.selectedMac);
      this.toast('正在连接此电池', 'none');
    });
  },

  deleteHistory(event) {
    const mac = event.currentTarget.dataset.mac;
    if (!mac) {
      return;
    }

    wx.showModal({
      title: '删除历史电池',
      content: '删除后可重新搜索并选择。',
      confirmText: '删除',
      confirmColor: '#ff5b6b',
      success: (res) => {
        if (!res.confirm) {
          return;
        }

        this.runTask(async () => {
          const selected = normalizeMac(this.data.selectedMac || this.data.state.selectedBmsMac) === normalizeMac(mac);
          await api.deleteBmsHistory(mac);
          if (selected) {
            this.setData({
              selectedMac: '',
              selectedName: ''
            });
          }
          this.refreshLists();
          this.toast('已删除', 'success');
        });
      }
    });
  },

  clearHistory() {
    wx.showModal({
      title: '清空历史电池',
      content: '这会清空本机保存的历史电池。',
      confirmText: '清空',
      confirmColor: '#ff5b6b',
      success: (res) => {
        if (!res.confirm) {
          return;
        }

        this.runTask(async () => {
          await api.clearBmsHistory();
          this.setData({
            selectedMac: '',
            selectedName: ''
          });
          this.refreshLists();
          this.toast('已清空', 'success');
        });
      }
    });
  },

  handleMessage(message) {
    if (message.type === 'select_ok') {
      this.toast('电池已连接', 'success');
      api.refreshStatus().catch(() => null);
      return;
    }

    if (message.type === 'scan_done') {
      this.toast('搜索完成', 'success');
    }
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
