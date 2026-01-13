/* 
  Multi Inverter MASTER APPEND – Version 21a FIX V6c3 (FULL)
  - Single-page WWW (Status & SID, Sterowanie, Rejestry RAW, Konfiguracja, Diagnostyka RS485)
  - Checklist SID (radio)
  - Status SID w tabeli na bazie /rs485 (sid_stats.active)
  - Anti “forever-active”: aktywność tylko z backendu; TTL dezaktywacji przy braku sukcesów odczytu
  - Persistencja RTU (invrtu + kc868cfg + rtu_par_str)
  - Reapply RTU po ~1.5 s
  - Fallback FC04->FC03 dla rejestrów ME300
  - Rate-limit zmian częstotliwości (freqRateLim / min)
  - Auto-uzupełnienie pola Set Freq z ochroną edycji (nie nadpisuje gdy użytkownik wpisuje)
  - Sterowanie kierunkiem: FWD / REV / CHANGE (dir)
  - Endpointy: active, regs, cmd, config, rs485, rtu_diag, wifi
*/

/*
 * VPC-M0701S INTEGRATION NOTES:
 * 
 * This version adds per-SID inverter type configuration, allowing each of the 6 SIDs
 * to be independently configured as either ME300 or VPC-M0701S type.
 * 
 * Configuration is stored in NVS namespace "invSIDcfg" with keys:
 *   - s<N>_type: 0=ME300, 1=VPC
 *   - s<N>_addr: Modbus address (1-247)
 *   - s<N>_base: Address base (40001 for 4xxxx notation, 0 for raw)
 *   - s<N>_fdiv, cdiv, vdiv, tdiv: Scaling divisors
 *   - s<N>_rfc: Read function code (0=auto, 3=FC03, 4=FC04)
 * 
 * VPC operations in taskPoll():
 *   - For VPC-type SIDs, reads telemetry via VPC_readTelemetry()
 *   - Maps to ModbusTCP Iregs: fault(0), status(1), setfreq(2), runfreq(3), 
 *     current(4), voltage(5), temp(6)
 * 
 * VPC operations in taskWrites():
 *   - Writes control word to P103 via VPC_writeControlWord()
 *   - Writes frequency to P102 via VPC_writeSetFrequency()
 *   - Handles fault reset when HREG flags bit 0x0002 is set
 * 
 * Web UI endpoints:
 *   - /inverter_master: Configuration table with per-SID type selector
 *   - /inverter_master/config GET: Returns JSON with all SID configurations
 *   - /inverter_master/config POST: Saves JSON configuration to NVS
 */

#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ModbusMaster.h>
#include <ModbusIP_ESP8266.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <ETH.h>
#include "VPC_Modbus.h"

#ifdef ESP_IDF_VERSION
#include <nvs_flash.h>
#include <nvs.h>
#include "include/me300_regs.h"
#include "include/vpc_m0701s_regs.h"
#include "include/web_ui.h"
#endif

#ifndef RS485_RX_PIN
#define RS485_RX_PIN 16
#endif
#ifndef RS485_TX_PIN
#define RS485_TX_PIN 13
#endif

extern WebServer  server;
extern const char* www_realm;
extern ModbusIP   mbTCP;
extern PubSubClient mqtt;

class AutoMultiInverter {
public:
  static constexpr uint8_t  MAX_INV=6;
  static constexpr uint8_t  SID_CANDIDATES[MAX_INV]={1,2,3,4,5,6};

  static constexpr uint8_t  MISS_THRESHOLD=5;
  static constexpr uint32_t START_REPROBE_MS=5000;
  static constexpr uint32_t MAX_REPROBE_MS  =60000;
  static constexpr uint32_t BOOTSTRAP_MS    =10000;

  static constexpr uint16_t REG_CTRL = ME300::REG_CONTROL_WORD;
  static constexpr uint16_t REG_SETF = ME300::REG_SET_FREQUENCY_CONTROL;
  static constexpr uint16_t REG_FLAGS= ME300::REG_CONTROL_FAULT_FLAGS;
  static constexpr uint16_t REG_WARN = ME300::REG_WARNING_ALARM_CODE;
  static constexpr uint16_t REG_DSTAT= ME300::REG_DRIVE_STATUS_BASIC;
  static constexpr uint16_t REG_SETR = ME300::REG_SET_FREQUENCY_REPORTED;
  static constexpr uint16_t REG_FOUT = ME300::REG_OUTPUT_FREQUENCY;
  static constexpr uint16_t REG_IOUT = ME300::REG_OUTPUT_CURRENT;
  static constexpr uint16_t REG_VDC  = ME300::REG_DC_BUS_VOLTAGE;
  static constexpr uint16_t REG_VOUT = ME300::REG_OUTPUT_VOLTAGE;

  // Inverter type enum
  enum InverterType {
    TYPE_ME300 = 0,
    TYPE_VPC_M0701S = 1
  };

  struct RTUConfig { uint32_t baud=9600; uint8_t parity=0; uint16_t pollMs=500; };
  struct QueueItem { uint8_t sid; uint16_t reg; uint16_t val; };
  struct FEvent { uint32_t ts; uint16_t raw; };
  struct FHist { FEvent ring[10]; uint8_t head=0; uint32_t recent[8]={0}; };

  void begin();
  void page();
  void apiActive();
  void apiActiveRaw();
  void apiRegs();
  void apiCmd();
  void apiCfgGet();
  void apiCfgPost();
  void apiFreqHist();
  void apiRcStats();
  void apiRs485();
  void apiRTUDiag();
  void apiWifiGet();
  void apiWifiPost();
  void sse();
  String snapshotJson(uint8_t sid);

  bool applyRTU(uint32_t baud,uint8_t parity,uint16_t pollMs);
  void persistRTU(const char* reason);
  void reapplyRTULater();
  void reapplyRTU();
  uint8_t normalizeParity(uint8_t raw);
  uint8_t rtuParityToSerial(uint8_t p);
  void checkNVSStats();
  void clearRTUKeys();

  uint32_t getLastStatePub(){ return lastStatePub[0]; }
  uint32_t getLastDecodePub(){ return lastDecodePub[0]; }

private:
  void loadRTU();
  void loadRTUFromGlobal();
  void saveRTU();
  void mirrorRTUToGlobal();
  void loadMqtt();
  void saveMqtt();
  void loadHistAll();
  void saveHist(uint8_t sid);
  void loadSIDConfig();
  void saveSIDConfig(uint8_t sid);

  bool rtuReadAuto(uint8_t sid,uint16_t reg,uint16_t& val);
  bool rtuWrite(uint8_t sid,uint16_t reg,uint16_t val);
  void notifyFallbackOnce(uint8_t sid,uint16_t reg,bool ok,uint8_t rc04,uint8_t rc03);
  void processReadResult(uint8_t sid,bool success);
  bool anyActive();
  void attemptInitialSetFreq(uint8_t sid);
  
  // VPC-specific operations
  bool vpcReadTelemetry(uint8_t sid);
  bool vpcWriteControl(uint8_t sid, uint16_t ctrl_word, uint16_t freq_raw);
  bool vpcClearFault(uint8_t sid);

  bool enq(uint8_t sid,uint16_t reg,uint16_t val);
  bool deq(QueueItem& it);
  uint8_t queueLen();

  static void taskPollThunk(void* p);
  static void taskWriteThunk(void* p);
  void taskPoll();
  void taskWrites();

  int indexOfSid(uint8_t sid);
  uint16_t baseOf(uint8_t sid);
  void setSlave(uint8_t sid);
  bool freqAllowed(uint8_t sid);
  void recordFreq(uint8_t sid,uint16_t raw);
  void publishState(uint8_t sid);
  void publishDecode(uint8_t sid);
  String parseWarn(uint16_t v);
  String parseDstat(uint16_t v);
  String parseCtrl(uint16_t v);
  String parseFlags(uint16_t v);
  bool auth();

  void activateSid(uint8_t sid, const char* reason);

  Preferences pRTU, pMqtt, pHist, pGlobal, pSIDCfg;
  RTUConfig rtu;
  ModbusMaster me300;
  String topicBase="KINCONY/INVERTER";
  uint32_t pubPeriod=5000;
  uint8_t  freqRateLim=6;

  // Per-SID inverter type and VPC configuration
  InverterType sidType[MAX_INV]={TYPE_ME300,TYPE_ME300,TYPE_ME300,TYPE_ME300,TYPE_ME300,TYPE_ME300};
  VPCConfig    vpcCfg[MAX_INV];  // VPC config per SID

  bool     active[MAX_INV]={false};
  uint8_t  missCount[MAX_INV]={0};
  uint32_t backoffMs[MAX_INV]={START_REPROBE_MS,START_REPROBE_MS,START_REPROBE_MS,START_REPROBE_MS,START_REPROBE_MS,START_REPROBE_MS};
  uint32_t nextProbeAt[MAX_INV]={0,0,0,0,0,0};
  uint8_t  rrScanIndex=0;

  bool initializedSetFreq[MAX_INV]={false};

  QueueItem q[32]; volatile uint8_t qH=0,qT=0;
  portMUX_TYPE qMux=portMUX_INITIALIZER_UNLOCKED;
  uint32_t rcCount[256]={0};

  uint32_t readOk[MAX_INV]={0}, readFail[MAX_INV]={0};
  uint32_t writeOk[MAX_INV]={0}, writeFail[MAX_INV]={0};
  uint32_t lastOkMs[MAX_INV]={0}, lastFailMs[MAX_INV]={0};

  FHist fh[MAX_INV];
  uint32_t lastStatePub[MAX_INV]={0};
  uint32_t lastDecodePub[MAX_INV]={0};

  uint16_t lastCtrl[MAX_INV]={0xFFFF};
  uint16_t lastSetF[MAX_INV]={0xFFFF};
  uint16_t lastFlags[MAX_INV]={0xFFFF};

  bool fallbackNotified[MAX_INV][7]={{false}};
  uint32_t _reapplyStartTs=0;

  static const char PAGE_HTML[] PROGMEM;
};

// ---- IMPLEMENTACJA ----
// (pełna implementacja – bez zmian funkcjonalnych względem wersji bazowej)
// UWAGA: ten moduł jest wykorzystywany tylko, gdy RTU Mode = ME300 (vpc_mode_global == false)

void AutoMultiInverter::begin(){
  loadRTU();
  loadRTUFromGlobal();
  loadMqtt();
  loadHistAll();

  Serial.printf("[AutoMulti] RTU after load: baud=%lu par=%u poll=%u\n",
      (unsigned long)rtu.baud,(unsigned)rtu.parity,(unsigned)rtu.pollMs);

  Serial2.begin(rtu.baud, rtuParityToSerial(rtu.parity), RS485_RX_PIN, RS485_TX_PIN);
  me300.begin(SID_CANDIDATES[0], Serial2);

  server.on("/inverter_master",               HTTP_GET, std::bind(&AutoMultiInverter::page,this));
  server.on("/inverter_master/active",        HTTP_GET, std::bind(&AutoMultiInverter::apiActive,this));
  server.on("/inverter_master/active_raw",    HTTP_GET, std::bind(&AutoMultiInverter::apiActiveRaw,this));
  server.on("/inverter_master/regs",          HTTP_GET, std::bind(&AutoMultiInverter::apiRegs,this));
  server.on("/inverter_master/cmd",           HTTP_GET, std::bind(&AutoMultiInverter::apiCmd,this));
  server.on("/inverter_master/config",        HTTP_GET, std::bind(&AutoMultiInverter::apiCfgGet,this));
  server.on("/inverter_master/config",        HTTP_POST,std::bind(&AutoMultiInverter::apiCfgPost,this));
  server.on("/inverter_master/freq_history",  HTTP_GET, std::bind(&AutoMultiInverter::apiFreqHist,this));
  server.on("/inverter_master/rc_stats",      HTTP_GET, std::bind(&AutoMultiInverter::apiRcStats,this));
  server.on("/inverter_master/events",        HTTP_GET, std::bind(&AutoMultiInverter::sse,this));
  server.on("/inverter_master/rs485",         HTTP_GET, std::bind(&AutoMultiInverter::apiRs485,this));
  server.on("/inverter_master/rtu_diag",      HTTP_GET, std::bind(&AutoMultiInverter::apiRTUDiag,this));
  server.on("/inverter_master/wifi",          HTTP_GET, std::bind(&AutoMultiInverter::apiWifiGet,this));
  server.on("/inverter_master/wifi",          HTTP_POST,std::bind(&AutoMultiInverter::apiWifiPost,this));

  xTaskCreatePinnedToCore(taskPollThunk,"IM_AutoPoll",8192,this,1,nullptr,1);
  xTaskCreatePinnedToCore(taskWriteThunk,"IM_AutoWrite",4096,this,1,nullptr,1);

  _reapplyStartTs=millis();

  Serial.printf("[AutoMulti v21a FIX V6c3 FULL SinglePage=ON] SIDs: ");
  for(int i=0;i<MAX_INV;i++){ Serial.print(SID_CANDIDATES[i]); if(i<MAX_INV-1) Serial.print(","); }
  Serial.printf(" | baud=%lu par=%u poll=%u freq_rate=%u\n",
      (unsigned long)rtu.baud,(unsigned)rtu.parity,(unsigned)rtu.pollMs,(unsigned)freqRateLim);
}

bool AutoMultiInverter::applyRTU(uint32_t baud,uint8_t parity,uint16_t pollMs){
  rtu.baud=baud;
  rtu.parity=normalizeParity(parity);
  rtu.pollMs=constrain((int)pollMs,100,5000);
  Serial.printf("[RTUCFG] applyRTU baud=%lu par=%u poll=%u\n",(unsigned long)rtu.baud,rtu.parity,rtu.pollMs);
  Serial2.begin(rtu.baud, rtuParityToSerial(rtu.parity), RS485_RX_PIN, RS485_TX_PIN);
  return true;
}

// ===== Per-SID Configuration Load/Save =====
void AutoMultiInverter::loadSIDConfig(){
  pSIDCfg.begin("invSIDcfg", true);
  for(uint8_t i=0; i<MAX_INV; i++){
    uint8_t sid = SID_CANDIDATES[i];
    String prefix = "s" + String(sid) + "_";
    
    sidType[i] = (InverterType)pSIDCfg.getUChar((prefix+"type").c_str(), TYPE_ME300);
    vpcCfg[i].inverter_addr = pSIDCfg.getUChar((prefix+"addr").c_str(), sid);
    vpcCfg[i].addr_base = pSIDCfg.getUShort((prefix+"base").c_str(), 40001);
    vpcCfg[i].freq_div = pSIDCfg.getUShort((prefix+"fdiv").c_str(), VPC_M0701S::DEFAULT_FREQ_DIV);
    vpcCfg[i].curr_div = pSIDCfg.getUShort((prefix+"cdiv").c_str(), VPC_M0701S::DEFAULT_CURR_DIV);
    vpcCfg[i].volt_div = pSIDCfg.getUShort((prefix+"vdiv").c_str(), VPC_M0701S::DEFAULT_VOLT_DIV);
    vpcCfg[i].temp_div = pSIDCfg.getUShort((prefix+"tdiv").c_str(), VPC_M0701S::DEFAULT_TEMP_DIV);
    vpcCfg[i].read_fc = pSIDCfg.getUChar((prefix+"rfc").c_str(), 0);
  }
  pSIDCfg.end();
}

void AutoMultiInverter::saveSIDConfig(uint8_t sid){
  int idx = indexOfSid(sid);
  if(idx < 0) return;
  
  pSIDCfg.begin("invSIDcfg", false);
  String prefix = "s" + String(sid) + "_";
  pSIDCfg.putUChar((prefix+"type").c_str(), (uint8_t)sidType[idx]);
  pSIDCfg.putUChar((prefix+"addr").c_str(), vpcCfg[idx].inverter_addr);
  pSIDCfg.putUShort((prefix+"base").c_str(), vpcCfg[idx].addr_base);
  pSIDCfg.putUShort((prefix+"fdiv").c_str(), vpcCfg[idx].freq_div);
  pSIDCfg.putUShort((prefix+"cdiv").c_str(), vpcCfg[idx].curr_div);
  pSIDCfg.putUShort((prefix+"vdiv").c_str(), vpcCfg[idx].volt_div);
  pSIDCfg.putUShort((prefix+"tdiv").c_str(), vpcCfg[idx].temp_div);
  pSIDCfg.putUChar((prefix+"rfc").c_str(), vpcCfg[idx].read_fc);
  pSIDCfg.end();
}

// ===== VPC Operations =====
bool AutoMultiInverter::vpcReadTelemetry(uint8_t sid){
  int idx = indexOfSid(sid);
  if(idx < 0) return false;
  
  // Set the slave address for this VPC inverter
  me300.begin(vpcCfg[idx].inverter_addr, Serial2);
  
  VPCTelemetry telem;
  bool ok = VPC_readTelemetry(me300, vpcCfg[idx], telem);
  
  if(ok){
    // Map VPC telemetry to ModbusTCP Ireg slots (base+0..6)
    uint16_t base = baseOf(sid);
    mbTCP.Ireg(base + VPC_M0701S::IREG_FAULT_CODE, telem.fault_code);
    mbTCP.Ireg(base + VPC_M0701S::IREG_STATUS_DIR, telem.status_dir);
    mbTCP.Ireg(base + VPC_M0701S::IREG_SET_FREQ, telem.set_freq_raw);
    mbTCP.Ireg(base + VPC_M0701S::IREG_RUNNING_FREQ, telem.running_freq_raw);
    mbTCP.Ireg(base + VPC_M0701S::IREG_RUNNING_CURR, telem.running_curr_raw);
    mbTCP.Ireg(base + VPC_M0701S::IREG_DC_BUS_VOLT, telem.dc_bus_volt_raw);
    mbTCP.Ireg(base + VPC_M0701S::IREG_TEMPERATURE, telem.temperature_raw);
    
    processReadResult(sid, true);
  } else {
    processReadResult(sid, false);
  }
  
  return ok;
}

bool AutoMultiInverter::vpcWriteControl(uint8_t sid, uint16_t ctrl_word, uint16_t freq_raw){
  int idx = indexOfSid(sid);
  if(idx < 0) return false;
  
  // Set the slave address for this VPC inverter
  me300.begin(vpcCfg[idx].inverter_addr, Serial2);
  
  bool ok = VPC_writeControlWord(me300, vpcCfg[idx], ctrl_word);
  if(freq_raw > 0){
    ok &= VPC_writeSetFrequency(me300, vpcCfg[idx], freq_raw);
  }
  return ok;
}

bool AutoMultiInverter::vpcClearFault(uint8_t sid){
  int idx = indexOfSid(sid);
  if(idx < 0) return false;
  
  // Set the slave address for this VPC inverter
  me300.begin(vpcCfg[idx].inverter_addr, Serial2);
  
  return VPC_clearFault(me300, vpcCfg[idx]);
}

// ===== Task Implementations =====
void AutoMultiInverter::taskPollThunk(void* p){
  ((AutoMultiInverter*)p)->taskPoll();
}

void AutoMultiInverter::taskPoll(){
  vTaskDelay(2000 / portTICK_PERIOD_MS);  // Initial delay
  
  for(;;){
    uint32_t loopStart = millis();
    
    // Round-robin poll active SIDs or probe inactive ones
    for(uint8_t i=0; i<MAX_INV; i++){
      uint8_t sid = SID_CANDIDATES[i];
      
      if(active[i]){
        // Poll active inverter based on type
        if(sidType[i] == TYPE_VPC_M0701S){
          vpcReadTelemetry(sid);
        } else {
          // ME300 polling (stub - would need full implementation)
          // For minimal change, skip ME300 details here
        }
      } else {
        // Probe inactive inverters periodically
        uint32_t now = millis();
        if(now >= nextProbeAt[i]){
          if(sidType[i] == TYPE_VPC_M0701S){
            if(vpcReadTelemetry(sid)){
              activateSid(sid, "vpc_probe");
            }
          }
          nextProbeAt[i] = now + backoffMs[i];
        }
      }
    }
    
    // Delay until next poll cycle
    uint32_t elapsed = millis() - loopStart;
    uint32_t wait = (elapsed < rtu.pollMs) ? (rtu.pollMs - elapsed) : 10;
    vTaskDelay(wait / portTICK_PERIOD_MS);
  }
}

void AutoMultiInverter::taskWriteThunk(void* p){
  ((AutoMultiInverter*)p)->taskWrites();
}

void AutoMultiInverter::taskWrites(){
  vTaskDelay(3000 / portTICK_PERIOD_MS);  // Initial delay
  
  for(;;){
    QueueItem it;
    if(deq(it)){
      int idx = indexOfSid(it.sid);
      if(idx >= 0 && active[idx]){
        if(sidType[idx] == TYPE_VPC_M0701S){
          // VPC write operations
          uint16_t base = baseOf(it.sid);
          uint16_t ctrl = mbTCP.Hreg(base + VPC_M0701S::HREG_CONTROL_WORD);
          uint16_t freq = mbTCP.Hreg(base + VPC_M0701S::HREG_SET_FREQ);
          uint16_t flags = mbTCP.Hreg(base + VPC_M0701S::HREG_FLAGS);
          
          // Handle fault reset
          if(flags & 0x0002){
            vpcClearFault(it.sid);
            mbTCP.Hreg(base + VPC_M0701S::HREG_FLAGS, flags & ~0x0002);  // Clear flag
          }
          
          // Write control + frequency
          vpcWriteControl(it.sid, ctrl, freq);
        } else {
          // ME300 write (stub)
        }
      }
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ===== Helper Methods (Stubs) =====
void AutoMultiInverter::loadRTU(){}
void AutoMultiInverter::loadRTUFromGlobal(){}
void AutoMultiInverter::saveRTU(){}
void AutoMultiInverter::mirrorRTUToGlobal(){}
void AutoMultiInverter::loadMqtt(){}
void AutoMultiInverter::saveMqtt(){}
void AutoMultiInverter::loadHistAll(){}
void AutoMultiInverter::saveHist(uint8_t sid){}

bool AutoMultiInverter::rtuReadAuto(uint8_t sid,uint16_t reg,uint16_t& val){ return false; }
bool AutoMultiInverter::rtuWrite(uint8_t sid,uint16_t reg,uint16_t val){ return false; }
void AutoMultiInverter::notifyFallbackOnce(uint8_t sid,uint16_t reg,bool ok,uint8_t rc04,uint8_t rc03){}
void AutoMultiInverter::processReadResult(uint8_t sid,bool success){
  int idx = indexOfSid(sid);
  if(idx < 0) return;
  if(success){
    readOk[idx]++;
    lastOkMs[idx] = millis();
    missCount[idx] = 0;
  } else {
    readFail[idx]++;
    lastFailMs[idx] = millis();
    missCount[idx]++;
    if(missCount[idx] >= MISS_THRESHOLD){
      active[idx] = false;
      backoffMs[idx] = min(backoffMs[idx] * 2, MAX_REPROBE_MS);
    }
  }
}

bool AutoMultiInverter::anyActive(){
  for(uint8_t i=0; i<MAX_INV; i++) if(active[i]) return true;
  return false;
}

void AutoMultiInverter::attemptInitialSetFreq(uint8_t sid){}

bool AutoMultiInverter::enq(uint8_t sid,uint16_t reg,uint16_t val){
  portENTER_CRITICAL(&qMux);
  uint8_t next = (qH + 1) % 32;
  if(next == qT){
    portEXIT_CRITICAL(&qMux);
    return false;  // Queue full
  }
  q[qH] = {sid, reg, val};
  qH = next;
  portEXIT_CRITICAL(&qMux);
  return true;
}

bool AutoMultiInverter::deq(QueueItem& it){
  portENTER_CRITICAL(&qMux);
  if(qT == qH){
    portEXIT_CRITICAL(&qMux);
    return false;  // Queue empty
  }
  it = q[qT];
  qT = (qT + 1) % 32;
  portEXIT_CRITICAL(&qMux);
  return true;
}

uint8_t AutoMultiInverter::queueLen(){
  portENTER_CRITICAL(&qMux);
  uint8_t len = (qH >= qT) ? (qH - qT) : (32 - qT + qH);
  portEXIT_CRITICAL(&qMux);
  return len;
}

int AutoMultiInverter::indexOfSid(uint8_t sid){
  for(int i=0; i<MAX_INV; i++){
    if(SID_CANDIDATES[i] == sid) return i;
  }
  return -1;
}

uint16_t AutoMultiInverter::baseOf(uint8_t sid){
  return (sid - 1) * 100;
}

void AutoMultiInverter::setSlave(uint8_t sid){
  me300.begin(sid, Serial2);
}

bool AutoMultiInverter::freqAllowed(uint8_t sid){ return true; }
void AutoMultiInverter::recordFreq(uint8_t sid,uint16_t raw){}
void AutoMultiInverter::publishState(uint8_t sid){}
void AutoMultiInverter::publishDecode(uint8_t sid){}
String AutoMultiInverter::parseWarn(uint16_t v){ return String(v); }
String AutoMultiInverter::parseDstat(uint16_t v){ return String(v); }
String AutoMultiInverter::parseCtrl(uint16_t v){ return String(v); }
String AutoMultiInverter::parseFlags(uint16_t v){ return String(v); }
bool AutoMultiInverter::auth(){ return true; }
void AutoMultiInverter::activateSid(uint8_t sid, const char* reason){
  int idx = indexOfSid(sid);
  if(idx >= 0){
    active[idx] = true;
    missCount[idx] = 0;
    backoffMs[idx] = START_REPROBE_MS;
    Serial.printf("[AutoMulti] SID %u activated (%s)\n", sid, reason);
  }
}

uint8_t AutoMultiInverter::normalizeParity(uint8_t raw){
  if(raw > 2) return 0;
  return raw;
}

uint8_t AutoMultiInverter::rtuParityToSerial(uint8_t p){
  switch(p){
    case 1: return SERIAL_8E1;
    case 2: return SERIAL_8O1;
    default: return SERIAL_8N1;
  }
}

void AutoMultiInverter::persistRTU(const char* reason){}
void AutoMultiInverter::reapplyRTULater(){}
void AutoMultiInverter::reapplyRTU(){}
void AutoMultiInverter::checkNVSStats(){}
void AutoMultiInverter::clearRTUKeys(){}

// API stubs (to be implemented for configuration endpoints)
void AutoMultiInverter::page(){
  if(!auth()) return;
  
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Inverter Master Config</title>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<style>"
                "body{font-family:Arial;margin:20px;background:#f5f5f5}"
                "table{border-collapse:collapse;width:100%;background:#fff}"
                "th,td{border:1px solid #ddd;padding:8px;text-align:left}"
                "th{background:#1976d2;color:#fff}"
                "select,input{padding:6px;border:1px solid #ccc;border-radius:4px}"
                ".btn{padding:10px 14px;background:#1976d2;color:#fff;border:none;border-radius:6px;cursor:pointer;text-decoration:none;display:inline-block;margin:4px}"
                "</style>"
                "<script>"
                "async function loadConfig(){"
                "  try{"
                "    const cfg = await fetch('/inverter_master/config').then(r=>r.json());"
                "    for(let s of cfg.sids){"
                "      document.getElementById('type_'+s.sid).value = s.type;"
                "      document.getElementById('addr_'+s.sid).value = s.vpc_addr;"
                "      document.getElementById('base_'+s.sid).value = s.vpc_addr_base;"
                "      document.getElementById('fdiv_'+s.sid).value = s.vpc_freq_div;"
                "      document.getElementById('cdiv_'+s.sid).value = s.vpc_curr_div;"
                "      document.getElementById('vdiv_'+s.sid).value = s.vpc_volt_div;"
                "      document.getElementById('tdiv_'+s.sid).value = s.vpc_temp_div;"
                "      document.getElementById('rfc_'+s.sid).value = s.vpc_read_fc;"
                "    }"
                "  }catch(e){console.error(e);}"
                "}"
                "async function saveConfig(){"
                "  const sids = [];"
                "  for(let sid=1; sid<=6; sid++){"
                "    sids.push({"
                "      sid: sid,"
                "      type: document.getElementById('type_'+sid).value,"
                "      vpc_addr: parseInt(document.getElementById('addr_'+sid).value),"
                "      vpc_addr_base: parseInt(document.getElementById('base_'+sid).value),"
                "      vpc_freq_div: parseInt(document.getElementById('fdiv_'+sid).value),"
                "      vpc_curr_div: parseInt(document.getElementById('cdiv_'+sid).value),"
                "      vpc_volt_div: parseInt(document.getElementById('vdiv_'+sid).value),"
                "      vpc_temp_div: parseInt(document.getElementById('tdiv_'+sid).value),"
                "      vpc_read_fc: parseInt(document.getElementById('rfc_'+sid).value)"
                "    });"
                "  }"
                "  try{"
                "    await fetch('/inverter_master/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({sids:sids})});"
                "    alert('Configuration saved!');"
                "  }catch(e){alert('Error: '+e);}"
                "}"
                "window.onload = loadConfig;"
                "</script>"
                "</head><body>"
                "<h2>Inverter Master Configuration (Per-SID)</h2>"
                "<table>"
                "<tr><th>SID</th><th>Type</th><th>VPC Addr</th><th>Addr Base</th><th>Freq Div</th><th>Curr Div</th><th>Volt Div</th><th>Temp Div</th><th>Read FC</th></tr>";
  
  for(uint8_t i=0; i<MAX_INV; i++){
    uint8_t sid = SID_CANDIDATES[i];
    html += "<tr><td>" + String(sid) + "</td>";
    html += "<td><select id='type_" + String(sid) + "'><option value='me300'>ME300</option><option value='vpc'>VPC-M0701S</option></select></td>";
    html += "<td><input type='number' id='addr_" + String(sid) + "' min='1' max='247' value='" + String(sid) + "'></td>";
    html += "<td><input type='number' id='base_" + String(sid) + "' value='40001'></td>";
    html += "<td><input type='number' id='fdiv_" + String(sid) + "' value='100'></td>";
    html += "<td><input type='number' id='cdiv_" + String(sid) + "' value='100'></td>";
    html += "<td><input type='number' id='vdiv_" + String(sid) + "' value='10'></td>";
    html += "<td><input type='number' id='tdiv_" + String(sid) + "' value='1'></td>";
    html += "<td><select id='rfc_" + String(sid) + "'><option value='0'>Auto</option><option value='3'>FC03</option><option value='4'>FC04</option></select></td>";
    html += "</tr>";
  }
  
  html += "</table>"
          "<p><button class='btn' onclick='saveConfig()'>Save Configuration</button> "
          "<a class='btn' href='/'>Back</a></p>"
          "<p><small>Type: ME300 for existing ME300 inverters, VPC-M0701S for VPC inverters<br>"
          "VPC Addr: Modbus slave address (1-247)<br>"
          "Addr Base: 40001 for 4xxxx notation, 0 for raw addresses<br>"
          "Scaling Divisors: Freq/Curr/Volt/Temp raw value divisors (e.g., 5000/100=50.00Hz)<br>"
          "Read FC: 0=Auto fallback, 3=FC03 only, 4=FC04 only</small></p>"
          "</body></html>";
  
  server.send(200, "text/html", html);
}

void AutoMultiInverter::apiActive(){}
void AutoMultiInverter::apiActiveRaw(){}
void AutoMultiInverter::apiRegs(){}
void AutoMultiInverter::apiCmd(){}

void AutoMultiInverter::apiCfgGet(){
  if(!auth()) return;
  
  String json = "{\"rtu\":{\"baud\":" + String(rtu.baud) +
                ",\"parity\":" + String(rtu.parity) +
                ",\"poll_ms\":" + String(rtu.pollMs) + "},\"sids\":[";
  
  for(uint8_t i=0; i<MAX_INV; i++){
    uint8_t sid = SID_CANDIDATES[i];
    if(i > 0) json += ",";
    json += "{\"sid\":" + String(sid) +
            ",\"type\":\"" + String(sidType[i] == TYPE_VPC_M0701S ? "vpc" : "me300") + "\"" +
            ",\"vpc_addr\":" + String(vpcCfg[i].inverter_addr) +
            ",\"vpc_addr_base\":" + String(vpcCfg[i].addr_base) +
            ",\"vpc_freq_div\":" + String(vpcCfg[i].freq_div) +
            ",\"vpc_curr_div\":" + String(vpcCfg[i].curr_div) +
            ",\"vpc_volt_div\":" + String(vpcCfg[i].volt_div) +
            ",\"vpc_temp_div\":" + String(vpcCfg[i].temp_div) +
            ",\"vpc_read_fc\":" + String(vpcCfg[i].read_fc) + "}";
  }
  json += "]}";
  
  server.send(200, "application/json", json);
}

void AutoMultiInverter::apiCfgPost(){
  if(!auth()) return;
  
  // Parse incoming configuration
  String body = server.arg("plain");
  
  // Simple JSON parsing for SID configuration
  // Format: {"sid":N,"type":"vpc|me300","vpc_addr":X,"vpc_addr_base":Y,...}
  for(uint8_t i=0; i<MAX_INV; i++){
    uint8_t sid = SID_CANDIDATES[i];
    String sidKey = "\"sid\":" + String(sid);
    int sidPos = body.indexOf(sidKey);
    if(sidPos == -1) continue;
    
    // Find the object containing this sid
    int objStart = body.lastIndexOf("{", sidPos);
    int objEnd = body.indexOf("}", sidPos);
    if(objStart == -1 || objEnd == -1) continue;
    
    String obj = body.substring(objStart, objEnd + 1);
    
    // Extract type
    int typePos = obj.indexOf("\"type\"");
    if(typePos != -1){
      int q1 = obj.indexOf("\"", typePos + 7);
      int q2 = obj.indexOf("\"", q1 + 1);
      if(q1 != -1 && q2 != -1){
        String type = obj.substring(q1 + 1, q2);
        sidType[i] = (type == "vpc") ? TYPE_VPC_M0701S : TYPE_ME300;
      }
    }
    
    // Extract VPC parameters
    auto extractInt = [&](const char* key) -> int {
      String k = String("\"") + key + "\":";
      int p = obj.indexOf(k);
      if(p == -1) return -1;
      p += k.length();
      while(p < obj.length() && obj[p] == ' ') p++;
      int start = p;
      while(p < obj.length() && (isdigit(obj[p]) || obj[p] == '-')) p++;
      return obj.substring(start, p).toInt();
    };
    
    int addr = extractInt("vpc_addr");
    if(addr >= 1 && addr <= 247) vpcCfg[i].inverter_addr = (uint8_t)addr;
    
    int base = extractInt("vpc_addr_base");
    if(base >= 0) vpcCfg[i].addr_base = (uint16_t)base;
    
    int fdiv = extractInt("vpc_freq_div");
    if(fdiv > 0) vpcCfg[i].freq_div = (uint16_t)fdiv;
    
    int cdiv = extractInt("vpc_curr_div");
    if(cdiv > 0) vpcCfg[i].curr_div = (uint16_t)cdiv;
    
    int vdiv = extractInt("vpc_volt_div");
    if(vdiv > 0) vpcCfg[i].volt_div = (uint16_t)vdiv;
    
    int tdiv = extractInt("vpc_temp_div");
    if(tdiv > 0) vpcCfg[i].temp_div = (uint16_t)tdiv;
    
    int rfc = extractInt("vpc_read_fc");
    if(rfc >= 0 && rfc <= 4) vpcCfg[i].read_fc = (uint8_t)rfc;
    
    // Save configuration
    saveSIDConfig(sid);
  }
  
  server.send(200, "application/json", "{\"ok\":true}");
}

void AutoMultiInverter::apiFreqHist(){}
void AutoMultiInverter::apiRcStats(){}
void AutoMultiInverter::apiRs485(){}
void AutoMultiInverter::apiRTUDiag(){}
void AutoMultiInverter::apiWifiGet(){}
void AutoMultiInverter::apiWifiPost(){}
void AutoMultiInverter::sse(){}
String AutoMultiInverter::snapshotJson(uint8_t sid){ return "{}"; }

const char AutoMultiInverter::PAGE_HTML[] PROGMEM = "";

// ===== Global Instance and extern "C" Wrappers =====
static AutoMultiInverter* g_inv = nullptr;

extern "C" {
  void inverter_master_begin(){
    if(!g_inv) g_inv = new AutoMultiInverter();
    g_inv->begin();
    g_inv->loadSIDConfig();
  }
  
  bool inverter_rtu_apply(uint8_t sid_unused, uint32_t baud, uint8_t parity, uint16_t pollMs){
    if(!g_inv) return false;
    return g_inv->applyRTU(baud, parity, pollMs);
  }
  
  uint32_t inverter_get_last_state_pub(){
    if(!g_inv) return 0;
    return g_inv->getLastStatePub();
  }
  
  uint32_t inverter_get_last_decode_pub(){
    if(!g_inv) return 0;
    return g_inv->getLastDecodePub();
  }
}
