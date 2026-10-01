#include "JkBle.h"
#include <Preferences.h>
#include <string.h>

static const char* SERVICE="FFE0";
static const char* WRITE_CHAR="FFE1";
static const char* NOTIFY_CHAR="FFE2"; // 仅作兼容回退；JK V20 常见通知也在 FFE1
JkBle* JkBle::instance_=nullptr;

JkBle::JkBle():client_(nullptr),ch_(nullptr),writeCh_(nullptr),notifyCh_(nullptr),counter_(0),lastRequest_(0),lastReconnectAttempt_(0),
  scanCount_(0),scanAttempt_(0),protocol32S_(true),configuredAddress_("") {
  instance_=this;
}

bool JkBle::begin(){
  NimBLEDevice::init("JK-C3-DISPLAY");
  NimBLEDevice::setPower(9);

  Preferences p;
  p.begin("jkcfg", true);
  configuredAddress_=p.getString("mac", "");
  protocol32S_=p.getBool("32s", true);
  g_bmsData.energyConsumptionWhKm=p.getFloat("whkm", 100.0f);
  if(g_bmsData.energyConsumptionWhKm<1.0f || g_bmsData.energyConsumptionWhKm>1000.0f)
    g_bmsData.energyConsumptionWhKm=100.0f;
  p.end();

  protocol_.setProtocol32S(protocol32S_);
  return true;
}

void JkBle::setConfiguredAddress(const String& mac){
  configuredAddress_=mac;
  Preferences p;
  p.begin("jkcfg", false);
  p.putString("mac", configuredAddress_);
  p.end();
}

void JkBle::setStatus(BmsBootState state, const String& message){
  g_bmsData.bootState=state;
  g_bmsData.statusMessage=message;
  g_bmsData.scanAttempt=scanAttempt_;
}

bool JkBle::isCandidate(const NimBLEAdvertisedDevice* d) const{
  String n=d->getName().c_str();
  n.toUpperCase();
  return d->isAdvertisingService(NimBLEUUID(SERVICE)) ||
         n.indexOf("JK")>=0 || n.indexOf("JIKONG")>=0 || n.indexOf("BMS")>=0;
}

uint8_t JkBle::scanDevices(uint32_t sec){
  scanCount_=0;
  NimBLEScan* s=NimBLEDevice::getScan();
  s->setActiveScan(true);
  s->setInterval(80);
  s->setWindow(60);

  NimBLEScanResults r=s->getResults(sec*1000,false);
  for(uint32_t i=0;i<(uint32_t)r.getCount() && scanCount_<JK_SCAN_RESULT_MAX;i++){
    const NimBLEAdvertisedDevice* d=r.getDevice(i);
    if(!isCandidate(d)) continue;

    scanItems_[scanCount_].address=d->getAddress().toString().c_str();
    scanItems_[scanCount_].addressType=d->getAddressType();
    Serial.printf("JK BLE: %s type=%u RSSI=%d\\n", scanItems_[scanCount_].address.c_str(), scanItems_[scanCount_].addressType, d->getRSSI());
    scanItems_[scanCount_].name=d->getName().c_str();
    if(scanItems_[scanCount_].name.length()==0) scanItems_[scanCount_].name="JK-BMS";
    scanItems_[scanCount_].rssi=d->getRSSI();
    scanCount_++;
  }

  // 本次扫描结束后立即停止并清理扫描缓存。
  // 连续 1/3 -> 2/3 -> 3/3 时如果不清理 NimBLE 扫描结果，
  // ESP32-C3 的堆内存可能持续增长，最终触发异常重启。
  s->stop();
  s->clearResults();

  return scanCount_;
}

bool JkBle::scanAndConnect(uint32_t sec, uint8_t attemptOverride){
  // 只有“BLE 已连接 + 已收到有效 JK 数据”才算真正连接成功。
  // 如果上一次只建立了 GATT 连接但没有有效协议数据，
  // 先释放旧 Client，再重新扫描，避免后续重试一直复用错误连接。
  if(connected() && g_bmsData.valid) return true;

  if(connected()){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr;
    writeCh_=nullptr;
    notifyCh_=nullptr;
    g_bmsData.online=false;
    g_bmsData.valid=false;
  }

  // attemptOverride 用于开机阶段：
  // main.cpp 会先把“1/3、2/3、3/3”画到屏幕，再进入阻塞式BLE扫描。
  // 这样第一次扫描期间不会一直停留在初始的 0/3。
  if(attemptOverride>=1 && attemptOverride<=3){
    scanAttempt_=attemptOverride;
  }else{
    scanAttempt_++;
    if(scanAttempt_>3) scanAttempt_=3;
  }
  g_bmsData.scanAttempt=scanAttempt_;
  setStatus(BOOT_SCANNING, "扫描蓝牙电池 " + String(scanAttempt_) + "/3");

  scanDevices(sec);

  if(configuredAddress_.length()){
    if(connectByAddress(configuredAddress_)) return true;
  }

  if(scanCount_==0){
    setStatus(BOOT_SCANNING, "第 " + String(scanAttempt_) + "/3 次未找到JK电池");
    return false;
  }

  uint8_t best=0;
  for(uint8_t i=1;i<scanCount_;i++){
    if(scanItems_[i].rssi>scanItems_[best].rssi) best=i;
  }
  return connectDeviceByIndex(best);
}

bool JkBle::connectDeviceByIndex(uint8_t index){
  if(index>=scanCount_) return false;
  return connectByAddress(scanItems_[index].address, scanItems_[index].addressType);
}

bool JkBle::connectByAddress(const String& address, uint8_t addressType){
  if(address.length()==0) return false;

  NimBLEAddress addr(address.c_str(), addressType);
  if(client_){
    if(client_->isConnected()) client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr;
    writeCh_=nullptr;
    notifyCh_=nullptr;
  }

  Serial.printf("JK BLE: connect %s type=%u (%s)\\n", address.c_str(), addressType, addressType == BLE_ADDR_PUBLIC ? "PUBLIC" : "RANDOM");
  setStatus(BOOT_CONNECTING, "连接 " + address);

  client_=NimBLEDevice::createClient();
  if(!client_) {
    setStatus(BOOT_SCANNING, "创建BLE客户端失败");
    return false;
  }

  client_->setConnectTimeout(5000);
  if(!client_->connect(addr)){
    setStatus(BOOT_SCANNING, "连接失败");
    g_bmsData.online=false;

    // 连接失败时立即释放本次 Client，避免下一轮扫描/连接继续占用堆。
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr;
    writeCh_=nullptr;
    notifyCh_=nullptr;
    return false;
  }

  // JK BMS 标准结构：
  // FFE0 = Service
  // FFE1 = 命令写入特征（部分型号同时支持 Notify/Read）
  // FFE2 = 数据通知特征（部分型号不存在）
  NimBLERemoteService* s=client_->getService(NimBLEUUID(SERVICE));
  if(!s){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr;
    writeCh_=nullptr;
    notifyCh_=nullptr;
    setStatus(BOOT_SCANNING, "找不到FFE0服务");
    return false;
  }

  // 关键修复：
  // 不能把 getUUID().toString() 与 "FFE1" 直接比较，因为 NimBLE
  // 返回的可能是完整 UUID 0000ffe1-0000-1000-8000-00805f9b34fb。
  // 直接按 NimBLEUUID 获取，兼容 16-bit/128-bit 表示。
  // JK V20/JK02：优先按 characteristic properties 判断通信方向。
  // 常见结构是 FFE1 同时承担 Write + Notify；FFE2 只作兼容回退。
  NimBLERemoteCharacteristic* ffe1=s->getCharacteristic(NimBLEUUID(WRITE_CHAR));
  NimBLERemoteCharacteristic* ffe2=s->getCharacteristic(NimBLEUUID(NOTIFY_CHAR));

  writeCh_=nullptr;
  notifyCh_=nullptr;

  if(ffe1 && (ffe1->canWriteNoResponse() || ffe1->canWrite()))
    writeCh_=ffe1;
  if(ffe1 && (ffe1->canNotify() || ffe1->canIndicate()))
    notifyCh_=ffe1;

  if(!notifyCh_ && ffe2 && (ffe2->canNotify() || ffe2->canIndicate()))
    notifyCh_=ffe2;
  if(!writeCh_ && ffe2 && (ffe2->canWriteNoResponse() || ffe2->canWrite()))
    writeCh_=ffe2;

  Serial.printf("JK BLE chars: FFE1 write=%d notify=%d; FFE2 write=%d notify=%d\\n",
                ffe1 ? (int)(ffe1->canWriteNoResponse() || ffe1->canWrite()) : 0,
                ffe1 ? (int)(ffe1->canNotify() || ffe1->canIndicate()) : 0,
                ffe2 ? (int)(ffe2->canWriteNoResponse() || ffe2->canWrite()) : 0,
                ffe2 ? (int)(ffe2->canNotify() || ffe2->canIndicate()) : 0);

  if(!writeCh_){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr;
    writeCh_=nullptr;
    notifyCh_=nullptr;
    setStatus(BOOT_SCANNING, "FFE1不可写");
    return false;
  }

  if(!notifyCh_){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr;
    writeCh_=nullptr;
    notifyCh_=nullptr;
    setStatus(BOOT_SCANNING, "FFE1/FFE2无通知能力");
    return false;
  }

  ch_=writeCh_;

  // 只有真正支持 Notify/Indicate 的特征才订阅。
  bool subscribed=false;
  if(notifyCh_->canNotify())
    subscribed=notifyCh_->subscribe(true, notifyCallback);
  else if(notifyCh_->canIndicate())
    subscribed=notifyCh_->subscribe(false, notifyCallback);

  if(!subscribed){
    client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
    ch_=nullptr;
    writeCh_=nullptr;
    notifyCh_=nullptr;
    setStatus(BOOT_SCANNING, "FFE1/FFE2通知订阅失败");
    return false;
  }

  configuredAddress_=address;
  g_bmsData.mac=address;
  g_bmsData.online=true;
  g_bmsData.bootState=BOOT_CONNECTED;
  g_bmsData.statusMessage="已连接JK电池";
  g_bmsData.scanAttempt=scanAttempt_;

  Preferences p;
  p.begin("jkcfg", false);
  p.putString("mac", configuredAddress_);
  p.end();

  // FFE1 是命令通道，不通过 readValue() 判断连接是否成功。
  // JK BMS 的实时数据由通知回调进入 handleNotification()。
  request(0x96);
  delay(100);
  request(0x97);
  lastRequest_=millis();
  return true;
}

void JkBle::notifyCallback(NimBLERemoteCharacteristic*,uint8_t* d,size_t n,bool){
  if(instance_) instance_->handleNotification(d,n);
}

void JkBle::handleNotification(const uint8_t* d,size_t n){
  // JK V20 调试：确认通知特征确实有原始数据。
  Serial.printf("JK RX notify len=%u: ", (unsigned)n);
  size_t dump=n<24?n:24;
  for(size_t i=0;i<dump;i++) Serial.printf("%02X ",d[i]);
  if(n>dump) Serial.print("...");
  Serial.println();

  static uint8_t rx[700];
  static size_t len=0;
  if(n>sizeof(rx) || len+n>sizeof(rx)){len=0;return;}
  memcpy(rx+len,d,n);
  len+=n;

  while(len>=5){
    size_t st=0;
    while(st+4<len && !(rx[st]==0x55&&rx[st+1]==0xAA&&rx[st+2]==0xEB&&rx[st+3]==0x90)) st++;
    if(st){
      memmove(rx,rx+st,len-st);
      len-=st;
      if(len<5) break;
    }

    size_t next=0;
    for(size_t i=4;i+4<len;i++){
      if(rx[i]==0x55&&rx[i+1]==0xAA&&rx[i+2]==0xEB&&rx[i+3]==0x90){
        next=i;
        break;
      }
    }

    if(next){
      if(protocol_.parseFrame(rx,next,g_bmsData)) g_bmsData.online=true;
      memmove(rx,rx+next,len-next);
      len-=next;
      continue;
    }

    size_t expected=protocol_.expectedFrameLength();
    if(len>=expected){
      if(protocol_.parseFrame(rx,expected,g_bmsData)) g_bmsData.online=true;
      memmove(rx,rx+expected,len-expected);
      len-=expected;
      continue;
    }
    break;
  }
}

void JkBle::request(uint8_t cmd){
  if(!writeCh_ && !ch_) return;

  NimBLERemoteCharacteristic* writer=writeCh_ ? writeCh_ : ch_;
  uint8_t f[20];
  protocol_.buildCommand(cmd,counter_++,f);

  // JK02 FFE1 常见为 Write Without Response。
  // 若设备只提供普通 Write，则使用带响应写入。
  if(writer->canWriteNoResponse())
    writer->writeValue(f,20,false);
  else if(writer->canWrite())
    writer->writeValue(f,20,true);
}

void JkBle::loop(){
  if(!connected()){
    g_bmsData.online=false;

    if(g_bmsData.bootState==BOOT_CONNECTED)
      setStatus(BOOT_SCANNING,"蓝牙已断开");

    if(g_bmsData.bootState!=BOOT_HOTSPOT &&
       millis()-lastReconnectAttempt_>=15000){
      lastReconnectAttempt_=millis();

      // 自动重连不能重新进入“1/3、2/3、3/3”的开机流程。
      // 保持当前扫描次数，避免失败后屏幕又回到 1/3。
      uint8_t reconnectAttempt = scanAttempt_;
      if(reconnectAttempt < 1 || reconnectAttempt > 3)
        reconnectAttempt = 1;

      if(!scanAndConnect(3, reconnectAttempt))
        g_bmsData.online=false;
    }
    return;
  }

  // 首次连接后如果尚未收到有效数据，缩短查询间隔；
  // 收到有效数据后恢复 5 秒一次，避免长期停留在全 0 的仪表盘。
  uint32_t requestInterval = g_bmsData.valid ? 5000UL : 1500UL;
  if(millis()-lastRequest_>requestInterval){
    request(0x96);
    lastRequest_=millis();
  }
}

bool JkBle::connected() const{
  return client_ && client_->isConnected();
}

void JkBle::releaseConnectionForHotspot(){
  // 连接失败但 GATT 已建立时，client_ 仍然可能保持连接。
  // 如果此时直接启动 WiFi AP，会让 BLE + WiFi 同时抢占 C3 堆内存，
  // 可能表现为“进入配网瞬间重启”。进入热点前必须彻底释放 Client。
  if(client_){
    if(client_->isConnected())
      client_->disconnect();
    NimBLEDevice::deleteClient(client_);
    client_=nullptr;
  }

  ch_=nullptr;
  writeCh_=nullptr;
  notifyCh_=nullptr;
  g_bmsData.online=false;
  g_bmsData.valid=false;

  NimBLEScan* s=NimBLEDevice::getScan();
  if(s){
    s->stop();
    s->clearResults();
  }
}
