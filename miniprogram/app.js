const ble = require('./utils/ble');

App({
  globalData: {
    ble
  },

  onLaunch() {
    ble.log('Mini program launched');
  },

  onHide() {
    // Keep the BLE link alive while the user switches pages briefly.
  }
});
