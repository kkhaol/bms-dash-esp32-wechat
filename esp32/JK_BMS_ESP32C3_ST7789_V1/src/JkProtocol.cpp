#include "JkProtocol.h"
#include <string.h>

JkProtocol::JkProtocol():protocol32S_(true){}

uint16_t JkProtocol::u16le(const uint8_t*p){
  return (uint16_t)p[0]|((uint16_t)p[1]<<8);
}

uint32_t JkProtocol::u32le(const uint8_t*p){
  return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

int16_t JkProtocol::s16le(const uint8_t*p){
  return (int16_t)u16le(p);
}

bool JkProtocol::validCrc(const uint8_t*p,size_t n){
  if(n<5) return false;
  uint8_t s=0;
  for(size_t i=0;i+1<n;i++) s+=p[i];
  return s==p[n-1];
}

int JkProtocol::detectOffset(const uint8_t*,size_t) const{
  return protocol32S_ ? 32 : 0;
}

bool JkProtocol::parseMainFrame(const uint8_t*p,size_t n,BmsData&o){
  int off=detectOffset(p,n);
  if(n<(size_t)(184+off)) return false;

  uint32_t mask=u32le(p+54+off);
  uint8_t maxCells=protocol32S_ ? 32 : 24;
  uint8_t cells=0;
  for(uint8_t i=0;i<maxCells;i++) if(mask & (1UL<<i)) cells++;

  if(cells==0) cells=maxCells;
  if(cells>JK_MAX_CELLS) cells=JK_MAX_CELLS;

  float minV=100,maxV=0;
  uint8_t minC=0,maxC=0;

  for(uint8_t i=0;i<JK_MAX_CELLS;i++) o.cellVoltage[i]=0;

  for(uint8_t i=0;i<cells;i++){
    size_t pos=6+i*2;
    if(pos+1>=n) break;
    float v=u16le(p+pos)*0.001f;
    o.cellVoltage[i]=v;
    if(v>0.5f&&v<6.0f){
      if(v<minV){minV=v;minC=i+1;}
      if(v>maxV){maxV=v;maxC=i+1;}
    }
  }

  o.cellCount=cells;
  o.minCellVoltage=minV<100?minV:0;
  o.maxCellVoltage=maxV;
  o.deltaCellVoltage=(maxV>0&&minV<100)?maxV-minV:0;
  o.minCell=minC;
  o.maxCell=maxC;

  o.totalVoltage=u32le(p+118+off)*0.001f;
  o.current=(int32_t)u32le(p+126+off)*0.001f;
  o.power=o.totalVoltage*o.current;

  o.temperature1=s16le(p+130+off)*0.1f;
  o.temperature2=s16le(p+132+off)*0.1f;
  o.mosTemperature=s16le(p+(off?112+off:134))*0.1f;

  if(protocol32S_) o.errors=u32le(p+134+off);
  else o.errors=u16le(p+136);

  o.balancingCurrent=u16le(p+138+off)*0.001f;
  o.balancing=p[140+off]!=0;
  o.soc=p[141+off];
  o.remainingCapacityAh=u32le(p+142+off)*0.001f;
  o.totalCapacityAh=u32le(p+146+off)*0.001f;

  // 剩余里程估算：剩余容量 × 当前电压 ÷ 单位里程能耗。
  // 例如 100Ah × 60V ÷ 100Wh/km = 60km。
  if(o.energyConsumptionWhKm>1.0f && o.totalVoltage>0.1f){
    o.remainingRangeKm=(o.remainingCapacityAh*o.totalVoltage)/
                       o.energyConsumptionWhKm;
  }else{
    o.remainingRangeKm=0;
  }

  o.charging=p[166+off]!=0;
  o.discharging=p[167+off]!=0;
  o.balancing=o.balancing || p[169+off]!=0;
  o.heating=p[183+off]!=0;

  o.valid=o.totalVoltage>0.1f;
  o.online=o.valid;
  o.updateMs=millis();
  return o.valid;
}

bool JkProtocol::parseFrame(const uint8_t*p,size_t n,BmsData&o){
  if(n<6||p[0]!=0x55||p[1]!=0xAA||p[2]!=0xEB||p[3]!=0x90) return false;
  if(!validCrc(p,n)) return false;
  if(p[4]==0x02) return parseMainFrame(p,n,o);
  return false;
}

void JkProtocol::buildCommand(uint8_t cmd,uint8_t counter,uint8_t out[20]){
  memset(out,0,20);
  out[0]=0xAA;out[1]=0x55;out[2]=0x90;out[3]=0xEB;
  out[4]=cmd;out[16]=counter;
  uint8_t s=0;
  for(int i=0;i<19;i++) s+=out[i];
  out[19]=s;
}
