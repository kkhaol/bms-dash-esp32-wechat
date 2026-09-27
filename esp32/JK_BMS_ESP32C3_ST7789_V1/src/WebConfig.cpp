#include "WebConfig.h"
#include <WiFi.h>
#include <Preferences.h>

WebConfig::WebConfig():server_(80),ble_(nullptr),active_(false){}

String WebConfig::jsonEscape(const String& s){
  String o;
  for(size_t i=0;i<s.length();i++){
    char c=s[i];
    if(c=='"') o+="\\"";
    else if(c=='\\') o+="\\\\";
    else if(c=='\n') o+="\\n";
    else o+=c;
  }
  return o;
}

String WebConfig::makeStatusJson(){
  String j="{";
  j+="\"online\":"+String(g_bmsData.online?"true":"false");
  j+=",\"state\":"+String((int)g_bmsData.bootState);
  j+=",\"message\":\""+jsonEscape(g_bmsData.statusMessage)+"\"";
  j+=",\"mac\":\""+jsonEscape(g_bmsData.mac)+"\"";
  j+=",\"name\":\""+jsonEscape(g_bmsData.deviceName)+"\"";
  j+=",\"ip\":\""+jsonEscape(WiFi.softAPIP().toString())+"\"";
  j+=",\"scanCount\":"+String(ble_?ble_->getScanCount():0);
  j+=",\"scanAttempt\":"+String(g_bmsData.scanAttempt);
  j+=",\"voltage\":"+String(g_bmsData.totalVoltage,3);
  j+=",\"current\":"+String(g_bmsData.current,3);
  j+=",\"power\":"+String(g_bmsData.power,1);
  j+=",\"remainingAh\":"+String(g_bmsData.remainingCapacityAh,2);
  j+=",\"remainingKm\":"+String(g_bmsData.remainingRangeKm,1);
  j+=",\"consumption\":"+String(g_bmsData.energyConsumptionWhKm,1);
  j+=",\"totalAh\":"+String(g_bmsData.totalCapacityAh,2);
  j+=",\"soc\":"+String(g_bmsData.soc,1);
  return j+"}";
}

String WebConfig::makePage(){
  return R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>JK BMS 设置</title>
<style>
*{box-sizing:border-box}
body{font-family:Arial,"Microsoft YaHei",sans-serif;background:#0b1015;color:#eee;margin:0;padding:14px}
.card{max-width:760px;margin:auto;background:#151c23;border-radius:16px;padding:18px;box-shadow:0 5px 25px #000}
h2{margin:0 0 12px}
button{padding:11px 15px;margin:5px;border:0;border-radius:9px;background:#1976d2;color:#fff;font-size:15px}
input,select{padding:11px;margin:5px 0;width:100%;border-radius:8px;border:1px solid #4a5662;background:#0d1217;color:#fff}
.item{padding:12px;border:1px solid #394652;border-radius:10px;margin:8px 0;background:#10161c}
.row{display:flex;gap:8px;align-items:center;justify-content:space-between}
.small{color:#aeb8c2;font-size:13px}
.ok{color:#45e27b}.warn{color:#ffc107}
.data{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:12px}
.data div{background:#0e141a;border-radius:9px;padding:10px}
.num{font-size:20px;font-weight:bold;color:#55d9ff}
.tip{color:#8e9aa5;font-size:13px;margin:4px 0 10px}
</style>
</head>
<body>
<div class="card">
<h2>JK BMS 蓝牙设置</h2>
<div id="status" class="small">读取状态...</div>

<label>保护板 MAC（可留空自动扫描）</label>
<input id="mac" placeholder="例如 AA:BB:CC:DD:EE:FF">

<label>协议型号</label>
<select id="proto">
<option value="32">JK02_32S</option>
<option value="24">JK02_24S</option>
</select>

<label>每公里耗电量（Wh/km）</label>
<input id="consumption" type="number" min="1" max="1000" step="1" value="100">
<div class="tip">用于估算剩余公里数：剩余容量 × 电压 ÷ 每公里耗电量</div>

<div>
<button onclick="scan()">扫描蓝牙电池</button>
<button onclick="save()">保存参数</button>
</div>
<div id="list"></div>

<div class="data">
<div>电压<br><span id="v" class="num">0 V</span></div>
<div>电流<br><span id="i" class="num">0 A</span></div>
<div>功率<br><span id="p" class="num">0 W</span></div>
<div>剩余容量<br><span id="ah" class="num">0 Ah</span></div>
<div>剩余公里数<br><span id="km" class="num">0 km</span></div>
<div>SOC<br><span id="soc" class="num">0%</span></div>
</div>
</div>

<script>
async function api(url,opt){return await (await fetch(url,opt)).json();}
async function status(){
  try{
    let s=await api('/api/status');
    document.getElementById('status').innerHTML=
      '<b class="'+(s.online?'ok':'warn')+'">'+(s.online?'已连接':'未连接')+
      '</b>　'+s.message+'<br>MAC: '+(s.mac||'未设置')+'　IP: '+s.ip;
    document.getElementById('v').textContent=s.voltage.toFixed(2)+' V';
    document.getElementById('i').textContent=s.current.toFixed(2)+' A';
    document.getElementById('p').textContent=s.power.toFixed(0)+' W';
    document.getElementById('ah').textContent=s.remainingAh.toFixed(2)+' Ah';
    document.getElementById('km').textContent=s.remainingKm.toFixed(1)+' km';
    document.getElementById('soc').textContent=s.soc.toFixed(0)+'%';
    document.getElementById('consumption').value=s.consumption.toFixed(0);
  }catch(e){}
}
async function scan(){
  document.getElementById('list').innerHTML='正在扫描蓝牙电池，请等待 5 秒...';
  let r=await api('/api/scan');
  let h='<h3>扫描结果（点击连接）</h3>';
  if(!r.items.length) h+='<div class="item">没有找到 JK / BMS 设备</div>';
  r.items.forEach((x,i)=>{
    h+='<div class="item"><div class="row"><b>'+x.name+'</b><span>RSSI '+x.rssi+' dBm</span></div>'+
       '<div class="small">'+x.address+'</div>'+
       '<button onclick="connectTo('+i+')">连接此电池</button></div>';
  });
  document.getElementById('list').innerHTML=h;
}
async function connectTo(i){
  document.getElementById('status').textContent='正在连接选中的蓝牙电池...';
  let r=await api('/api/connect?index='+i);
  document.getElementById('status').textContent=r.message;
  status();
}
async function save(){
  let fd=new FormData();
  fd.append('mac',document.getElementById('mac').value);
  fd.append('proto',document.getElementById('proto').value);
  fd.append('consumption',document.getElementById('consumption').value);
  let r=await api('/api/save',{method:'POST',body:fd});
  alert(r.message);
  status();
}
status();
setInterval(status,1000);
</script>
</body></html>)HTML";
}

void WebConfig::begin(JkBle* ble){
  ble_=ble;
  active_=true;

  // 从 Preferences 读取上次保存的单位里程耗电量。
  Preferences p;
  p.begin("jkcfg",true);
  g_bmsData.energyConsumptionWhKm=p.getFloat("whkm",100.0f);
  p.end();

  if(g_bmsData.energyConsumptionWhKm<1.0f || g_bmsData.energyConsumptionWhKm>1000.0f)
    g_bmsData.energyConsumptionWhKm=100.0f;

  server_.on("/",HTTP_GET,[this](){handleRoot();});
  server_.on("/api/status",HTTP_GET,[this](){handleStatus();});
  server_.on("/api/scan",HTTP_GET,[this](){handleScan();});
  server_.on("/api/connect",HTTP_GET,[this](){handleConnect();});
  server_.on("/api/save",HTTP_POST,[this](){handleSave();});
  server_.onNotFound([this](){handleNotFound();});
  server_.begin();
}

void WebConfig::loop(){
  if(active_) server_.handleClient();
}

void WebConfig::handleRoot(){
  server_.send(200,"text/html; charset=utf-8",makePage());
}

void WebConfig::handleStatus(){
  server_.send(200,"application/json; charset=utf-8",makeStatusJson());
}

void WebConfig::handleScan(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\",\"items\":[]}");
    return;
  }
  uint8_t count=ble_->scanDevices(5);
  String j="{\"message\":\"扫描完成\",\"items\":[";
  for(uint8_t i=0;i<count;i++){
    if(i) j+=",";
    const JkScanItem& x=ble_->getScanItem(i);
    j+="{\"name\":\""+jsonEscape(x.name)+"\",\"address\":\""+
      jsonEscape(x.address)+"\",\"rssi\":"+String(x.rssi)+"}";
  }
  j+="]}";
  server_.send(200,"application/json; charset=utf-8",j);
}

void WebConfig::handleConnect(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\"}");
    return;
  }
  if(!server_.hasArg("index")){
    server_.send(400,"application/json","{\"message\":\"缺少index\"}");
    return;
  }
  int index=server_.arg("index").toInt();
  bool ok=ble_->connectDeviceByIndex((uint8_t)index);
  String j="{\"ok\":" + String(ok?"true":"false")+
           ",\"message\":\""+String(ok?"连接成功":"连接失败")+"\"}";
  server_.send(ok?200:500,"application/json; charset=utf-8",j);
}

void WebConfig::handleSave(){
  if(!ble_){
    server_.send(500,"application/json","{\"message\":\"BLE未初始化\"}");
    return;
  }

  String mac=server_.hasArg("mac")?server_.arg("mac"):"";
  mac.trim();

  bool is32=server_.hasArg("proto") ? server_.arg("proto")=="32" : true;

  float whkm=server_.hasArg("consumption") ? server_.arg("consumption").toFloat() : 100.0f;
  if(whkm<1.0f) whkm=1.0f;
  if(whkm>1000.0f) whkm=1000.0f;

  ble_->setConfiguredAddress(mac);
  ble_->setProtocol32S(is32);

  g_bmsData.energyConsumptionWhKm=whkm;

  Preferences p;
  p.begin("jkcfg",false);
  p.putBool("32s",is32);
  p.putFloat("whkm",whkm);
  p.end();

  // 保存后立即重新计算一次，网页不用重启即可看到新结果。
  if(g_bmsData.totalVoltage>0.1f && whkm>1.0f){
    g_bmsData.remainingRangeKm=
      (g_bmsData.remainingCapacityAh*g_bmsData.totalVoltage)/whkm;
  }

  server_.send(200,"application/json; charset=utf-8",
               "{\"message\":\"参数已保存，下次开机自动使用\"}");
}

void WebConfig::handleNotFound(){
  server_.send(404,"text/plain; charset=utf-8","404");
}
