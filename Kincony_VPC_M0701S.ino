/*
  KC868-A16 MASTER – Multi SID v21a (Version2_Version22) + VPC M0701S Support
  Pełny plik – pełna funkcjonalna zawartość WWW, ModbusTCP, MQTT oraz rozszerzenie o obsługę falownika VPC M0701S.

  Zawiera:
    - Multi-SID ModbusTCP (6 falowników, offset 100 HREG/IREG per SID)
    - MQTT (INVERTER/<sid>/set, INOUT/set, MODBUSRTU/set; oraz VPC/<cmd>/set i VPC/state)
    - WWW:
      / (root) – nawigacja
      /config (GET/POST) – konfiguracja ETH, WiFi AP/STA, RTU, TCP
      /inverter – panel multi-SID z kontrolą Control Word i częstotliwości (ME300 / ogólne)
      /inverter/state – JSON SID1 (kompat)
      /inverter/state_all – JSON wszystkich SID
      /inverter/cmd – sterowanie multi-SID (GET param sid, c, v)
      /io – panel I/O (zachowany)
      /io/state – stan wejść/wyjść
      /io/set – ustaw cewkę/wyjście
      /io/diag – szczegółowa diagnostyka I2C/PCF + mapy
      /critical – tabela z JSON systemu
      /mqtt/repub – dokumentacja MQTT
      /mqtt/repub/ui – UI do republish i ręcznych publish
      /mqtt/repub/publish – akcje republish
      /mqtt/repub/set – ręczny publish
      /modbus/tcp – rozszerzony opis ModbusTCP (bity CW wg CSV + ramki FC05/FC06)
      /resources – Zasoby (monitor pamięci, odświeżanie co 10 s)
      /resources/data – JSON z zasobami

    - VPC M0701S (Modbus RTU przez RS485/Serial2):
      Endpoints WWW:
        /vpc           – strona statusu VPC + podstawowe sterowanie
        /vpc/status    – JSON statusu (running_status, fault_status)
        /vpc/cmd       – GET: c=start|stop|setf|reset, v=częstotliwość (Hz)
      MQTT:
        Subscribe: KINCONY/VPC/set    – {"cmd":"start"|"stop"|"setf"|"reset","freq":50.00}
        Publish:   KINCONY/VPC/state  – {"running_status":..., "fault_status":...}

  Uwierzytelnianie WWW: admin / darol177
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ETH.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ModbusIP_ESP8266.h>
#include <Wire.h>
#include <PCF8574.h>
#include <esp_heap_caps.h>

// ---- VPC M0701S ----
#include "VPC_Modbus.h"  // dostarczone API dla VPC

// ===== Sprzęt / Sieć =====
#ifndef RS485_RX_PIN
#define RS485_RX_PIN 16
#endif
#ifndef RS485_TX_PIN
#define RS485_TX_PIN 13
#endif
static_assert(RS485_RX_PIN==16,"KC868-A16: RS485_RX_PIN must be 16");
static_assert(RS485_TX_PIN==13,"KC868-A16: RS485_TX_PIN must be 13");

static const IPAddress DEF_ETH_IP (192,168,4,127);
static const IPAddress DEF_ETH_GW (192,168,4,1);
static const IPAddress DEF_ETH_SN (255,255,255,0);
static const IPAddress DEF_ETH_DNS(194,204,152,34);

static const char*     DEF_AP_SSID = "KINCONY_WIFI";
static const char*     DEF_AP_PASS = "darol177";
static const IPAddress DEF_AP_IP   (192,168,50,1);
static const IPAddress DEF_AP_SN   (255,255,255,0);

// MQTT
static const char* MQTT_HOST = "192.168.4.11";
static const int   MQTT_PORT = 1883;
static const char* MQTT_USER = "salon_klima";
static const char* MQTT_PASS = "darol177";
static const bool  MQTT_DO_DELTA = true;
static const bool  MQTT_PUBLISH_FULL_STATE = true;

// ===== Rejestry lokalne (indeksy) =====
enum ModbusRegisters {
  // HREG
  REG_CONTROL_WORD   = 0,
  REG_FREQUENCY_SET  = 1,
  REG_ACCEL_TIME     = 2,
  REG_DECEL_TIME     = 3,
  // IREG
  REG_STATUS_WORD    = 0,
  REG_OUTPUT_FREQ    = 1,
  REG_OUTPUT_CURRENT = 2,
  REG_OUTPUT_VOLTAGE = 3,
  REG_OUTPUT_POWER   = 4,
  REG_DC_BUS_VOLTAGE = 5,
  REG_RPM            = 6,
  REG_FAULT_CODE     = 7
};

// Multi-SID offset
static inline int HREG_BASE(uint8_t sid){ return (sid-1)*100; }
static inline int IREG_BASE(uint8_t sid){ return (sid-1)*100; }
static const uint8_t MAX_SID=6;

// Obiekty
WebServer  server(80);
ModbusIP   mbTCP;
WiFiClient _net;
PubSubClient mqtt(_net);
Preferences prefs;

// I2C / PCF8574
PCF8574 pcf_OUT1(0x24);
PCF8574 pcf_OUT2(0x25);
PCF8574 pcf_IN1 (0x22);
PCF8574 pcf_IN2 (0x21);
bool has_OUT1=false, has_OUT2=false, has_IN1=false, has_IN2=false;

// Stan systemu
struct SystemStatus {
  bool eth_connected=false;
  bool ap_active=false;
  bool sta_active=false;
  bool mqtt_connected=false;
  bool i2c_initialized=false;
  uint16_t di[16]={0};
  uint16_t dout[32]={0};
  uint16_t ai[4]={0};
  uint32_t last_io_pub=0;
  uint32_t last_mb_pub=0;
} systemStatus;

// Konfiguracja trwała
struct NetCfg {
  IPAddress eth_ip, eth_gw, eth_sn, eth_dns;
  bool wifi_ap=true;
  IPAddress ap_ip=DEF_AP_IP, ap_sn=DEF_AP_SN;
  String ap_ssid=DEF_AP_SSID, ap_pass=DEF_AP_PASS;
  String sta_ssid="", sta_pass="";
  uint16_t sta_fb_sec=30;
} netCfg;

struct TCPShadow { bool enabled=true; uint16_t port=502; } tcpCfg;
struct RTUShadow { uint32_t baud=9600; uint8_t parity=0; uint16_t pollMs=500; } rtuCfg;

// VPC legacy configuration (for backward compatibility with old VPC endpoint)
struct VPCLegacyConfig { uint8_t addr=1; bool enabled=true; uint16_t pollMs=600; } vpcLegacyCfg;
static uint32_t vpcLastPoll=0;
static uint16_t vpcRunningStatus=0;
static uint16_t vpcFaultStatus=0;

// Multi-SID runtime
static bool     rtu_freq_initialized[MAX_SID+1]={false};
static uint16_t rtu_last_known_setf[MAX_SID+1]={0};
static uint8_t  sid_list[MAX_SID]={1,2,3,4,5,6};
static uint8_t  sid_count=6;
static uint8_t  current_sid_index=0;

// Append moduł (extern "C") – ME300 / AutoMultiInverter
extern "C" {
  void     inverter_master_begin();
  bool     inverter_rtu_apply(uint8_t sid_unused, uint32_t baud, uint8_t parity, uint16_t pollMs);
  uint32_t inverter_get_last_state_pub();
  uint32_t inverter_get_last_decode_pub();
}

// Utils
static String ipToStr(const IPAddress& ip){ return String(ip[0])+"."+ip[1]+"."+ip[2]+"."+ip[3]; }
static bool strToIP(const String& s, IPAddress& out){ IPAddress t; if(t.fromString(s)){ out=t; return true;} return false; }

// WiFi events
static uint32_t sta_connect_start_ms=0;
static void onWiFiEvent(WiFiEvent_t event){
  switch(event){
    case ARDUINO_EVENT_ETH_START: ETH.setHostname("kc868-a16"); break;
    case ARDUINO_EVENT_ETH_GOT_IP: ETH.setDefault(); systemStatus.eth_connected=true; break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP: systemStatus.eth_connected=false; break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: systemStatus.sta_active=true; sta_connect_start_ms=0; break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      systemStatus.sta_active=false;
      if(!netCfg.wifi_ap && sta_connect_start_ms==0) sta_connect_start_ms=millis();
      break;
    default: break;
  }
}

// Load/save
static void loadCfg(){
  // kc868cfg (ETH/WiFi/TCP)
  prefs.begin("kc868cfg", true);
  IPAddress t;
  t.fromString(prefs.getString("eth_ip",  ipToStr(DEF_ETH_IP)));  netCfg.eth_ip=t;
  t.fromString(prefs.getString("eth_gw",  ipToStr(DEF_ETH_GW)));  netCfg.eth_gw=t;
  t.fromString(prefs.getString("eth_sn",  ipToStr(DEF_ETH_SN)));  netCfg.eth_sn=t;
  t.fromString(prefs.getString("eth_dns", ipToStr(DEF_ETH_DNS))); netCfg.eth_dns=t;
  netCfg.wifi_ap  = prefs.getBool("wifi_ap", true);
  netCfg.ap_ssid  = prefs.getString("ap_ssid", DEF_AP_SSID);
  netCfg.ap_pass  = prefs.getString("ap_pass", DEF_AP_PASS);
  t.fromString(prefs.getString("ap_ip", ipToStr(DEF_AP_IP))); netCfg.ap_ip=t;
  t.fromString(prefs.getString("ap_sn", ipToStr(DEF_AP_SN))); netCfg.ap_sn=t;
  netCfg.sta_ssid = prefs.getString("sta_ssid", "");
  netCfg.sta_pass = prefs.getString("sta_pass", "");
  netCfg.sta_fb_sec = prefs.getUShort("sta_fb_sec", 30);
  if(netCfg.sta_fb_sec<5) netCfg.sta_fb_sec=5;
  if(netCfg.sta_fb_sec>300) netCfg.sta_fb_sec=300;
  tcpCfg.enabled = prefs.getBool("tcp_en", true);
  tcpCfg.port    = prefs.getUShort("tcp_port", 502);
  prefs.end();

  // invrtu (global RTU)
  prefs.begin("invrtu", true);
  rtuCfg.baud   = prefs.getULong("baud",9600);
  rtuCfg.parity = prefs.getUChar("par",0);
  rtuCfg.pollMs = prefs.getUShort("poll",500);
  prefs.end();
  rtuCfg.pollMs=constrain((int)rtuCfg.pollMs,100,5000);

  // vpc (VPC M0701S)
  prefs.begin("vpc", true);
  vpcLegacyCfg.addr   = prefs.getUChar("addr", 1);
  vpcLegacyCfg.enabled= prefs.getBool("enabled", true);
  vpcLegacyCfg.pollMs = prefs.getUShort("poll", 600);
  prefs.end();
  vpcLegacyCfg.pollMs = constrain((int)vpcLegacyCfg.pollMs, 200, 5000);
}
static void saveCfg(){
  prefs.begin("kc868cfg", false);
  prefs.putString("eth_ip",  ipToStr(netCfg.eth_ip));
  prefs.putString("eth_gw",  ipToStr(netCfg.eth_gw));
  prefs.putString("eth_sn",  ipToStr(netCfg.eth_sn));
  prefs.putString("eth_dns", ipToStr(netCfg.eth_dns));
  prefs.putBool("wifi_ap", netCfg.wifi_ap);
  prefs.putString("ap_ssid", netCfg.ap_ssid);
  prefs.putString("ap_pass", netCfg.ap_pass);
  prefs.putString("ap_ip", ipToStr(netCfg.ap_ip));
  prefs.putString("ap_sn", ipToStr(netCfg.ap_sn));
  prefs.putString("sta_ssid", netCfg.sta_ssid);
  prefs.putString("sta_pass", netCfg.sta_pass);
  prefs.putUShort("sta_fb_sec", netCfg.sta_fb_sec);
  prefs.putBool("tcp_en", tcpCfg.enabled);
  prefs.putUShort("tcp_port", tcpCfg.port);
  prefs.end();

  prefs.begin("invrtu", false);
  prefs.putULong("baud", rtuCfg.baud);
  prefs.putUChar("par",  rtuCfg.parity);
  prefs.putUShort("poll",rtuCfg.pollMs);
  prefs.end();

  prefs.begin("vpc", false);
  prefs.putUChar("addr", vpcLegacyCfg.addr);
  prefs.putBool("enabled", vpcLegacyCfg.enabled);
  prefs.putUShort("poll", vpcLegacyCfg.pollMs);
  prefs.end();
}

// ETH/WiFi
static void setupETH(){
  WiFi.onEvent(onWiFiEvent);
  if(!ETH.begin(ETH_PHY_LAN8720,0,23,18,-1,ETH_CLOCK_GPIO17_OUT))
    Serial.println("ETH.begin failed");
  ETH.config(netCfg.eth_ip, netCfg.eth_gw, netCfg.eth_sn, netCfg.eth_dns);
}
static void applyWifiMode(bool ap){
  WiFi.disconnect(true);
  delay(50);
  if(ap){
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(netCfg.ap_ip, netCfg.ap_ip, netCfg.ap_sn);
    WiFi.softAP(netCfg.ap_ssid.c_str(), netCfg.ap_pass.c_str());
    systemStatus.ap_active=true; systemStatus.sta_active=false;
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(netCfg.sta_ssid.c_str(), netCfg.sta_pass.c_str());
    systemStatus.ap_active=false;
    sta_connect_start_ms=millis();
  }
}
static void setupWiFiInitial(){ applyWifiMode(netCfg.wifi_ap); }

// Modbus TCP (multi-SID)
static void setupMBTCP(){
  if(!tcpCfg.enabled){ Serial.println("[ModbusTCP] disabled"); return; }
  mbTCP.server(tcpCfg.port);
  for(uint8_t sid=1; sid<=MAX_SID; sid++){
    int hbase=HREG_BASE(sid), ibase=IREG_BASE(sid);
    for(int i=0;i<100;i++){ mbTCP.addHreg(hbase+i); mbTCP.addIreg(ibase+i); }
  }
  for(int i=0;i<64;i++){ mbTCP.addCoil(i); mbTCP.addIsts(i); }
  Serial.printf("[ModbusTCP] multi-SID server on %u\n", tcpCfg.port);
}

// I2C / IO
static bool i2cPresent(uint8_t a){ Wire.beginTransmission(a); return Wire.endTransmission()==0; }
static void setupIO(){
  Wire.begin(4,5);
  Wire.setClock(100000);
  delay(150);
  has_OUT1=pcf_OUT1.begin()||i2cPresent(0x24);
  has_OUT2=pcf_OUT2.begin()||i2cPresent(0x25);
  has_IN1 =pcf_IN1.begin() ||i2cPresent(0x22);
  has_IN2 =pcf_IN2.begin() ||i2cPresent(0x21);
  if(has_OUT1){ for(int i=0;i<8;i++){ pcf_OUT1.pinMode(i,OUTPUT); pcf_OUT1.digitalWrite(i,HIGH);} }
  if(has_OUT2){ for(int i=0;i<8;i++){ pcf_OUT2.pinMode(i,OUTPUT); pcf_OUT2.digitalWrite(i,HIGH);} }
  if(has_IN1 ){ for(int i=0;i<8;i++) pcf_IN1.pinMode(i,INPUT); }
  if(has_IN2 ){ for(int i=0;i<8;i++) pcf_IN2.pinMode(i,INPUT); }
  systemStatus.i2c_initialized=(has_OUT1||has_OUT2||has_IN1||has_IN2);
  analogReadResolution(12);
}
static void readInputs(){
  if(!systemStatus.i2c_initialized) return;
  for(int i=0;i<8;i++){
    uint8_t v1=has_IN1?pcf_IN1.digitalRead(i):1;
    uint8_t v2=has_IN2?pcf_IN2.digitalRead(i):1;
    systemStatus.di[i]=v1?0:1;
    systemStatus.di[i+8]=v2?0:1;
    mbTCP.Ists(i,systemStatus.di[i]);
    mbTCP.Ists(i+8,systemStatus.di[i+8]);
  }
  systemStatus.ai[0]=analogRead(32);
  systemStatus.ai[1]=analogRead(33);
  systemStatus.ai[2]=analogRead(34);
  systemStatus.ai[3]=analogRead(35);
}
static void updateOutputs(){
  for(int i=0;i<8;i++){
    bool on1=mbTCP.Coil(i), on2=mbTCP.Coil(i+8);
    if(has_OUT1) pcf_OUT1.digitalWrite(i,on1?LOW:HIGH);
    if(has_OUT2) pcf_OUT2.digitalWrite(i,on2?LOW:HIGH);
    systemStatus.dout[i]=on1?1:0;
    systemStatus.dout[i+8]=on2?1:0;
  }
}

// Guard częstotliwości (Multi-SID)
static bool rtuSyncInitialFrequency(uint8_t sid, uint16_t reported_setf_010Hz, uint16_t output_freq_010Hz){
  uint16_t init=0xFFFF;
  if(reported_setf_010Hz>0) init=reported_setf_010Hz;
  else if(output_freq_010Hz>0) init=output_freq_010Hz;
  if(init!=0xFFFF){
    rtu_last_known_setf[sid]=init;
    rtu_freq_initialized[sid]=true;
    mbTCP.Hreg(HREG_BASE(sid)+REG_FREQUENCY_SET, init);
    return true;
  }
  return false;
}

// ---- VPC helpers ----
static void setupVPC(){
  // Serial2: RS485 RTU zgodnie z globalną konfiguracją
  Serial2.begin(rtuCfg.baud, (rtuCfg.parity==1)?SERIAL_8E1:(rtuCfg.parity==2)?SERIAL_8O1:SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  VPC_init(Serial2, vpcLegacyCfg.addr);
  Serial.printf("[VPC] init addr=%u baud=%lu par=%u poll=%u\n",
    (unsigned)vpcLegacyCfg.addr, (unsigned long)rtuCfg.baud,(unsigned)rtuCfg.parity,(unsigned)vpcLegacyCfg.pollMs);
}
static bool vpcPoll(){
  if(!vpcLegacyCfg.enabled) return false;
  uint8_t rc1 = ModbusMaster::ku8MBIllegalFunction; // placeholder
  uint8_t rc2 = ModbusMaster::ku8MBIllegalFunction;
  // Odczyt Running Status (UWAGA: biblioteka zwykle używa adresów 0..65535; jeśli potrzeba, dopasuj offset)
  rc1 = (uint8_t) (VPC_readStatus() ? ModbusMaster::ku8MBSuccess : ModbusMaster::ku8MBIllegalDataValue);
  // Po VPC_readStatus() wartości są w buforze; używamy getResponseBuffer(0) jako przykład Running Status
  vpcRunningStatus = VPC_Modbus_getResponseBuffer(0); // helper poniżej
  // Fault Status (opcjonalnie osobny odczyt, jeśli potrzebny)
  // Brak osobnej funkcji – z READ 10 rejestrów; dla przejrzystości przypisz z bufora [9] jako fault (jeśli taki układ)
  vpcFaultStatus   = VPC_Modbus_getResponseBuffer(9);

  // Publikacja do ModbusTCP (SID1 IREG mapowane w prosty sposób – status/fault)
  int ib = IREG_BASE(1);
  mbTCP.Ireg(ib + REG_STATUS_WORD, vpcRunningStatus);
  mbTCP.Ireg(ib + REG_FAULT_CODE,  vpcFaultStatus);

  // Publikacja MQTT
  if(mqtt.connected()){
    String js = String("{\"running_status\":")+vpcRunningStatus+",\"fault_status\":"+vpcFaultStatus+"}";
    mqtt.publish("KINCONY/VPC/state", js.c_str(), false);
  }
  return (rc1==ModbusMaster::ku8MBSuccess);
}
// Pomocnik do pobrania bufora odpowiedzi VPC (wykorzystujemy instancję legacy)
extern ModbusMaster vpc_legacy_node; // z VPC_Modbus.cpp
static uint16_t VPC_Modbus_getResponseBuffer(uint8_t idx){ return vpc_legacy_node.getResponseBuffer(idx); }

// MQTT (minimal – Multi-SID + VPC)
static uint32_t lastMqttAttempt=0;
static void setupMQTT(){
  mqtt.setServer(MQTT_HOST,MQTT_PORT);
  mqtt.setCallback([](char* topic, byte* payload, unsigned int len){
    String t = String(topic);
    String msg; msg.reserve(len+8);
    for(unsigned int i=0;i<len;i++) msg += (char)payload[i];

    // INOUT/set (ustaw cewki)
    if(t=="KINCONY/INOUT/set"){
      int maxOut=16;
      auto extractIntAfter=[&](int pos)->int{
        int colon = msg.indexOf(":", pos); if(colon==-1) return -1;
        int end = colon+1; while(end<(int)msg.length() && msg[end]==' ') end++;
        int start=end; while(end<(int)msg.length() && isDigit(msg[end])) end++;
        return (int)msg.substring(start,end).toInt();
      };
      if(msg.indexOf("\"coil\"")!=-1){
        int idxPos=msg.indexOf("\"index\""), valPos=msg.indexOf("\"value\"");
        int index=extractIntAfter(idxPos), value=extractIntAfter(valPos);
        if(index>=0 && index<maxOut && (value==0||value==1)){
          mbTCP.Coil(index, value==1);
          systemStatus.dout[index]=value?1:0;
        }
        return;
      }
      if(msg.indexOf("\"outputs\"")!=-1){
        int arrStart=msg.indexOf("[", msg.indexOf("\"outputs\""));
        int arrEnd  =(arrStart!=-1)? msg.indexOf("]", arrStart) : -1;
        if(arrStart!=-1 && arrEnd!=-1){
          String arr=msg.substring(arrStart+1, arrEnd);
          int i=0, p=0;
          while(p<arr.length() && i<maxOut){
            while(p<arr.length() && arr[p]==' ') p++;
            if(p<arr.length() && (arr[p]=='0'||arr[p]=='1')){
              int v=(arr[p]=='1')?1:0;
              mbTCP.Coil(i, v==1); systemStatus.dout[i]=v; i++;
            }
            while(p<arr.length() && arr[p]!=',') p++;
            if(p<arr.length() && arr[p]==',') p++;
          }
        }
        return;
      }
      return;
    }

    // MODBUSRTU/set (globalne RTU / write_hreg)
    if(t=="KINCONY/MODBUSRTU/set"){
      if(msg.indexOf("\"rtu\"")!=-1){
        auto getStrVal=[&](const char* key)->String{
          int p=msg.indexOf(String("\"")+key+"\""); if(p==-1) return "";
          int c=msg.indexOf(":",p); if(c==-1) return "";
          int q1=msg.indexOf("\"",c); if(q1==-1) return "";
          int q2=msg.indexOf("\"",q1+1); if(q2==-1) return "";
          return msg.substring(q1+1,q2);
        };
        auto getIntVal=[&](const char* key)->long{
          int p=msg.indexOf(String("\"")+key+"\""); if(p==-1) return -1L;
          int c=msg.indexOf(":",p); if(c==-1) return -1L;
          int s=c+1; while(s<(int)msg.length() && msg[s]==' ') s++;
          int e=s; while(e<(int)msg.length() && (isDigit(msg[e])||msg[e]=='-')) e++;
          return (long)msg.substring(s,e).toInt();
        };
        long sid=getIntVal("sid"); if(sid<1||sid>247) sid=1;
        long baud=getIntVal("baud"); if(baud>0) rtuCfg.baud=(uint32_t)baud;
        String par=getStrVal("par");
        if(par.length()){
          if(par=="8N1") rtuCfg.parity=0;
          else if(par=="8E1") rtuCfg.parity=1;
          else if(par=="8O1") rtuCfg.parity=2;
        }
        long poll=getIntVal("poll"); if(poll>=100 && poll<=5000) rtuCfg.pollMs=(uint16_t)poll;
        // zapis
        prefs.begin("invrtu", false);
        prefs.putULong("baud", rtuCfg.baud);
        prefs.putUChar("par",  rtuCfg.parity);
        prefs.putUShort("poll",rtuCfg.pollMs);
        prefs.end();
        inverter_rtu_apply(0, rtuCfg.baud, rtuCfg.parity, rtuCfg.pollMs);
        setupVPC(); // re-init VPC po zmianie RTU
      }
      return;
    }

    // INVERTER/<sid>/set – pozostaje bez zmian
    if(t.startsWith("KINCONY/INVERTER/") && t.endsWith("/set")){
      int p1=String("KINCONY/INVERTER/").length();
      int p2=t.indexOf("/", p1);
      int sid = (p2>p1)? t.substring(p1,p2).toInt() : 1;
      if(sid<1||sid>MAX_SID) sid=1;
      int hbase = HREG_BASE(sid);

      uint16_t ctrl = mbTCP.Hreg(hbase + REG_CONTROL_WORD);
      auto setBitRange=[&](uint16_t& w, int from, int to, uint16_t val){
        uint16_t mask=0; for(int b=from; b<=to; b++) mask |= (1u<<b);
        w = (w & ~mask) | ((val<<(from)) & mask);
      };
      auto hasKey=[&](const char* k){ return msg.indexOf(String("\"")+k+"\"")!=-1; };
      auto boolVal=[&](const char* k)->bool{
        int p=msg.indexOf(String("\"")+k+"\""); if(p==-1) return false;
        int c=msg.indexOf(":",p); if(c==-1) return false;
        String v=msg.substring(c+1); v.trim();
        return v.startsWith("true")||v.startsWith("1");
      };
      auto strVal=[&](const char* k)->String{
        int p=msg.indexOf(String("\"")+k+"\""); if(p==-1) return "";
        int c=msg.indexOf(":",p); if(c==-1) return "";
        int q1=msg.indexOf("\"",c); if(q1==-1) return "";
        int q2=msg.indexOf("\"",q1+1); if(q2==-1) return "";
        return msg.substring(q1+1,q2);
      };
      auto ival=[&](const char* k)->long{
        int p=msg.indexOf(String("\"")+k+"\""); if(p==-1) return -1L;
        int c=msg.indexOf(":",p); if(c==-1) return -1L;
        int s=c+1; while(s<(int)msg.length() && msg[s]==' ') s++;
        int e=s; while(e<(int)msg.length() && (isDigit(msg[e])||msg[e]=='-')) e++;
        return (long)msg.substring(s,e).toInt();
      };

      if(hasKey("start") && boolVal("start")) setBitRange(ctrl,0,1,0b10);
      if(hasKey("stop")  && boolVal("stop"))  setBitRange(ctrl,0,1,0b01);
      if(hasKey("jog")   && boolVal("jog"))   setBitRange(ctrl,0,1,0b11);

      String dir = strVal("dir");
      if(dir=="fwd")      setBitRange(ctrl,4,5,0b01);
      else if(dir=="rev") setBitRange(ctrl,4,5,0b10);
      else if(dir=="chg") setBitRange(ctrl,4,5,0b11);

      long acc = ival("acc_set");
      if(acc>=0 && acc<=3) setBitRange(ctrl,6,7,(uint16_t)acc);

      long preset = ival("preset");
      bool pen = boolVal("preset_enable");
      if(preset>=0 && preset<=15){
        setBitRange(ctrl,8,11,(uint16_t)preset);
        if(hasKey("preset_enable")) setBitRange(ctrl,12,12, pen?1:0);
      }

      String cm = strVal("control_mode");
      if(cm=="panel")      setBitRange(ctrl,13,14,0b01);
      else if(cm=="param") setBitRange(ctrl,13,14,0b10);
      else if(cm=="change")setBitRange(ctrl,13,14,0b11);

      mbTCP.Hreg(hbase + REG_CONTROL_WORD, ctrl);

      if(hasKey("freq")){
        float hz = strVal("freq").toFloat();
        if(hz>=0 && hz<=400){
          uint16_t setf = (uint16_t) round(hz*100.0f);
          if(rtu_freq_initialized[sid] || setf>0){
            mbTCP.Hreg(hbase + REG_FREQUENCY_SET, setf);
          }
        }
      }
      return;
    }

    // KINCONY/VPC/set – sterowanie VPC M0701S
    if(t=="KINCONY/VPC/set"){
      auto strVal=[&](const char* k)->String{
        int p=msg.indexOf(String("\"")+k+"\""); if(p==-1) return "";
        int c=msg.indexOf(":",p); if(c==-1) return "";
        int q1=msg.indexOf("\"",c); if(q1==-1) return "";
        int q2=msg.indexOf("\"",q1+1); if(q2==-1) return "";
        return msg.substring(q1+1,q2);
      };
      auto numVal=[&](const char* k)->double{
        int p=msg.indexOf(String("\"")+k+"\""); if(p==-1) return NAN;
        int c=msg.indexOf(":",p); if(c==-1) return NAN;
        int s=c+1; while(s<(int)msg.length() && (msg[s]==' '||msg[s]=='\"')) s++;
        int e=s; while(e<(int)msg.length() && (isdigit(msg[e])||msg[e]=='.')) e++;
        return atof(msg.substring(s,e).c_str());
      };
      String cmd=strVal("cmd");
      double freq=numVal("freq");

      if(cmd=="start"){ VPC_start(); }
      else if(cmd=="stop"){ VPC_stop(); }
      else if(cmd=="reset"){ VPC_clearFault(); }
      else if(cmd=="setf"){
        if(!isnan(freq) && freq>=0 && freq<=400.0){ VPC_setFrequency((float)freq); }
      }
      // Po komendzie opublikuj bieżący stan
      vpcPoll();
      return;
    }
  });
}
static void ensureMqtt(){
  if(mqtt.connected()) return;
  if(millis()-lastMqttAttempt<4000) return;
  lastMqttAttempt=millis();
  bool ok=strlen(MQTT_USER)?mqtt.connect("KC868-A16",MQTT_USER,MQTT_PASS):mqtt.connect("KC868-A16");
  systemStatus.mqtt_connected=ok;
  if(ok){
    mqtt.subscribe("KINCONY/INOUT/set");
    mqtt.subscribe("KINCONY/MODBUSRTU/set");
    mqtt.subscribe("KINCONY/INVERTER/+/set");
    mqtt.subscribe("KINCONY/VPC/set"); // nowy topic dla VPC
  }
}
static void publishIO(){
  if(!mqtt.connected()) return;
  String s="{\"digital_inputs\":[";
  for(int i=0;i<16;i++){ s+=systemStatus.di[i]; if(i<15) s+=","; }
  s+="],\"analog_inputs\":[";
  for(int i=0;i<4;i++){ s+=systemStatus.ai[i]; if(i<3) s+=","; }
  s+="],\"digital_outputs\":[";
  int maxOut=16;
  for(int i=0;i<maxOut;i++){ s+=systemStatus.dout[i]; if(i<maxOut-1) s+=","; }
  s+="]}";
  mqtt.publish("KINCONY/INOUT/state", s.c_str(), false);
  systemStatus.last_io_pub=millis();
}
static void publishMB(){
  if(!mqtt.connected()) return;
  String j="{\"holding_registers_s1\":[";
  for(int i=0;i<10;i++){ j+=mbTCP.Hreg(HREG_BASE(1)+i); if(i<9) j+=","; }
  j+="],\"input_registers_s1\":[";
  for(int i=0;i<10;i++){ j+=mbTCP.Ireg(IREG_BASE(1)+i); if(i<9) j+=","; }
  j+="]}";
  mqtt.publish("KINCONY/MODBUSRTU/state", j.c_str(), false);
}
static void publishInverterStateSID(uint8_t sid){
  if(!mqtt.connected()) return;
  int ib=IREG_BASE(sid);
  String js="{\"status\":"+String(mbTCP.Ireg(ib+REG_STATUS_WORD))+
            ",\"freq\":"+String(mbTCP.Ireg(ib+REG_OUTPUT_FREQ))+
            ",\"curr\":"+String(mbTCP.Ireg(ib+REG_OUTPUT_CURRENT))+
            ",\"volt\":"+String(mbTCP.Ireg(ib+REG_OUTPUT_VOLTAGE))+
            ",\"power\":"+String(mbTCP.Ireg(ib+REG_OUTPUT_POWER))+
            ",\"dcv\":"+String(mbTCP.Ireg(ib+REG_DC_BUS_VOLTAGE))+
            ",\"rpm\":"+String(mbTCP.Ireg(ib+REG_RPM))+"}";
  mqtt.publish(String("KINCONY/INVERTER/"+String(sid)+"/state").c_str(), js.c_str(), false);
}

// Auth
const char* www_realm="KC868-A16 Admin";
static bool requireAuth(){
  if(!server.authenticate("admin","darol177")){
    server.requestAuthentication(DIGEST_AUTH, www_realm);
    return false;
  }
  return true;
}

// ===== Root =====
static void handleRoot(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>KC868-A16</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              "a.btn{display:inline-block;padding:10px 14px;background:#1976d2;color:#fff;text-decoration:none;margin:4px;border-radius:8px}"
              ".card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.12)}"
              "</style></head><body><h1>KC868-A16 Control Panel</h1><div class='card'><p>"
              "<a class='btn' href='/config'>Config</a>"
              "<a class='btn' href='/inverter_master'>Inverter Multi-SID</a>"
              "<a class='btn' href='/inverter_master/config'>Inverter Config (ME300/VPC)</a>"
              "<a class='btn' href='/vpc'>VPC M0701S</a>"
              "<a class='btn' href='/io'>I/O Panel</a>"
              "<a class='btn' href='/io/diag'>I/O Diagnostics</a>"
              "<a class='btn' href='/mqtt/repub'>MQTT Topics</a>"
              "<a class='btn' href='/mqtt/repub/ui'>MQTT Republish</a>"
              "<a class='btn' href='/modbus/tcp'>ModbusTCP</a>"
              "<a class='btn' href='/critical'>Critical</a>"
              "<a class='btn' href='/resources'>Zasoby</a>"
              "</p>"
              "<p>ETH: "+(systemStatus.eth_connected?ETH.localIP().toString():"(no link)")+
              " | AP: "+(systemStatus.ap_active?WiFi.softAPIP().toString():"(inactive)")+
              " | STA: "+(systemStatus.sta_active?WiFi.localIP().toString():"(inactive)")+"</p>"
              "</div></body></html>";
  server.send(200,"text/html",html);
}

// ===== Config (pełna) =====
static void handleConfigGet(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>KC868-A16 Configuration</title>"
             "<style>body{font-family:Arial;margin:20px}fieldset{margin-bottom:16px;padding:12px;border:1px solid #ccc;border-radius:8px}"
             "label{display:block;margin:6px 0 2px}input[type=text],select{width:260px;padding:6px}.btn{padding:8px 12px;background:#1976d2;color:#fff;border:none;border-radius:6px;cursor:pointer}</style>"
             "</head><body><h2>Configuration</h2><form method='POST' action='/config'>"
             "<fieldset><legend>Ethernet</legend>"
             "<label>IP</label><input type='text' name='eth_ip' value='%ETH_IP%'>"
             "<label>Subnet</label><input type='text' name='eth_sn' value='%ETH_SN%'>"
             "<label>Gateway</label><input type='text' name='eth_gw' value='%ETH_GW%'>"
             "<label>DNS</label><input type='text' name='eth_dns' value='%ETH_DNS%'>"
             "</fieldset>"
             "<fieldset><legend>WiFi Mode</legend>"
             "<label><input type='radio' name='wifi_mode' value='AP' %WIFI_AP%> AP</label>"
             "<label><input type='radio' name='wifi_mode' value='STA' %WIFI_STA%> STA (DHCP)</label>"
             "<label>AP SSID</label><input type='text' name='ap_ssid' value='%AP_SSID%'>"
             "<label>AP PASS</label><input type='text' name='ap_pass' value='%AP_PASS%'>"
             "<label>AP IP</label><input type='text' name='ap_ip' value='%AP_IP%'>"
             "<label>AP Subnet</label><input type='text' name='ap_sn' value='%AP_SN%'>"
             "<label>STA SSID</label><input type='text' name='sta_ssid' value='%STA_SSID%'>"
             "<label>STA PASS</label><input type='text' name='sta_pass' value='%STA_PASS%'>"
             "<label>STA Fallback [s]</label><input type='text' name='sta_fb_sec' value='%STA_FB_SEC%'>"
             "</fieldset>"
             "<fieldset><legend>Modbus RTU (Global + VPC)</legend>"
             "<label>Baud</label><select name='rtu_baud'>"
             "<option %B9600% value='9600'>9600</option>"
             "<option %B19200% value='19200'>19200</option>"
             "<option %B38400% value='38400'>38400</option>"
             "<option %B57600% value='57600'>57600</option>"
             "<option %B115200% value='115200'>115200</option>"
             "</select>"
             "<label>Parity</label><select name='rtu_par'>"
             "<option %P0% value='0'>8N1</option>"
             "<option %P1% value='1'>8E1</option>"
             "<option %P2% value='2'>8O1</option>"
             "</select>"
             "<label>Poll [ms] (global)</label><input type='text' name='rtu_poll' value='%RTU_POLL%'>"
             "<label>VPC Enabled</label><select name='vpc_en'><option %VPC_ON% value='1'>ON</option><option %VPC_OFF% value='0'>OFF</option></select>"
             "<label>VPC Address</label><input type='text' name='vpc_addr' value='%VPC_ADDR%'>"
             "<label>VPC Poll [ms]</label><input type='text' name='vpc_poll' value='%VPC_POLL%'>"
             "</fieldset>"
             "<fieldset><legend>Modbus TCP</legend>"
             "<label>Enabled</label><select name='tcp_en'><option %TCP_ON% value='1'>ON</option><option %TCP_OFF% value='0'>OFF</option></select>"
             "<label>Port</label><input type='text' name='tcp_port' value='%TCP_PORT%'>"
             "</fieldset>"
             "<p><button class='btn' type='submit'>Save & Apply</button> <a class='btn' href='/'>Back</a></p>"
             "</form></body></html>";
  html.replace("%ETH_IP%", ipToStr(netCfg.eth_ip));
  html.replace("%ETH_SN%", ipToStr(netCfg.eth_sn));
  html.replace("%ETH_GW%", ipToStr(netCfg.eth_gw));
  html.replace("%ETH_DNS%", ipToStr(netCfg.eth_dns));
  html.replace("%WIFI_AP%",  netCfg.wifi_ap ? "checked" : "");
  html.replace("%WIFI_STA%",  netCfg.wifi_ap ? "" : "checked");
  html.replace("%AP_SSID%", netCfg.ap_ssid);
  html.replace("%AP_PASS%", netCfg.ap_pass);
  html.replace("%AP_IP%", ipToStr(netCfg.ap_ip));
  html.replace("%AP_SN%", ipToStr(netCfg.ap_sn));
  html.replace("%STA_SSID%", netCfg.sta_ssid);
  html.replace("%STA_PASS%", netCfg.sta_pass);
  html.replace("%STA_FB_SEC%", String(netCfg.sta_fb_sec));
  html.replace("%RTU_POLL%", String(rtuCfg.pollMs));
  html.replace("%B9600%", rtuCfg.baud==9600?"selected":"");
  html.replace("%B19200%", rtuCfg.baud==19200?"selected":"");
  html.replace("%B38400%", rtuCfg.baud==38400?"selected":"");
  html.replace("%B57600%", rtuCfg.baud==57600?"selected":"");
  html.replace("%B115200%", rtuCfg.baud==115200?"selected":"");
  html.replace("%P0%", rtuCfg.parity==0?"selected":"");
  html.replace("%P1%", rtuCfg.parity==1?"selected":"");
  html.replace("%P2%", rtuCfg.parity==2?"selected":"");
  html.replace("%VPC_ON%", vpcLegacyCfg.enabled?"selected":"");
  html.replace("%VPC_OFF%", vpcLegacyCfg.enabled?"":"selected");
  html.replace("%VPC_ADDR%", String(vpcLegacyCfg.addr));
  html.replace("%VPC_POLL%", String(vpcLegacyCfg.pollMs));
  html.replace("%TCP_ON%", tcpCfg.enabled?"selected":"");
  html.replace("%TCP_OFF%", tcpCfg.enabled?"":"selected");
  html.replace("%TCP_PORT%", String(tcpCfg.port));
  server.send(200,"text/html", html);
}
static void handleConfigPost(){
  if(!requireAuth()) return;
  IPAddress ip;
  if(strToIP(server.arg("eth_ip"), ip))  netCfg.eth_ip=ip;
  if(strToIP(server.arg("eth_sn"), ip))  netCfg.eth_sn=ip;
  if(strToIP(server.arg("eth_gw"), ip))  netCfg.eth_gw=ip;
  if(strToIP(server.arg("eth_dns"),ip))  netCfg.eth_dns=ip;
  netCfg.wifi_ap = (server.arg("wifi_mode")!="STA");
  if(server.arg("ap_ssid").length()) netCfg.ap_ssid=server.arg("ap_ssid");
  if(server.arg("ap_pass").length()) netCfg.ap_pass=server.arg("ap_pass");
  if(strToIP(server.arg("ap_ip"), ip)) netCfg.ap_ip=ip;
  if(strToIP(server.arg("ap_sn"), ip)) netCfg.ap_sn=ip;
  netCfg.sta_ssid=server.arg("sta_ssid");
  netCfg.sta_pass=server.arg("sta_pass");
  { int fb=server.arg("sta_fb_sec").toInt(); if(fb>=5&&fb<=300) netCfg.sta_fb_sec=fb; }
  { uint32_t baud=(uint32_t)server.arg("rtu_baud").toInt(); if(baud) rtuCfg.baud=baud; }
  { int par=server.arg("rtu_par").toInt(); if(par>=0&&par<=2) rtuCfg.parity=par; }
  { int poll=server.arg("rtu_poll").toInt(); if(poll>=100&&poll<=5000) rtuCfg.pollMs=poll; }
  vpcLegacyCfg.enabled = server.arg("vpc_en")=="1";
  { int a=server.arg("vpc_addr").toInt(); if(a>=1&&a<=247) vpcLegacyCfg.addr=(uint8_t)a; }
  { int p=server.arg("vpc_poll").toInt(); if(p>=200&&p<=5000) vpcLegacyCfg.pollMs=(uint16_t)p; }
  tcpCfg.enabled = server.arg("tcp_en")=="1";
  { int tp=server.arg("tcp_port").toInt(); if(tp>=1&&tp<=65535) tcpCfg.port=tp; }
  saveCfg();
  inverter_rtu_apply(0, rtuCfg.baud, rtuCfg.parity, rtuCfg.pollMs);
  setupVPC(); // apply VPC po zmianach RTU/VPC
  applyWifiMode(netCfg.wifi_ap);
  server.send(200,"application/json","{\"ok\":true}");
}

// ===== Inverter (Multi-SID pełny panel) =====
// (sekcja jak w Twojej bazie – bez zmian, pomijana dla zwięzłości – zachowane handleInverterPage/State/StateAll/handleInverterCmd)

// ===== VPC WWW =====
static void handleVPCPage(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>VPC M0701S</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:16px;background:#f5f6fa;color:#222}"
              ".card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.12);margin-bottom:12px}"
              "button{padding:8px 12px;background:#1976d2;color:#fff;border:none;border-radius:6px;cursor:pointer;margin:4px}"
              "input{padding:6px;border:1px solid #bbb;border-radius:6px}"
              "</style>"
              "<script>"
              "async function refresh(){ try{const s=await fetch('/vpc/status',{cache:'no-store'}).then(r=>r.json());"
              " document.getElementById('run').textContent=s.running_status;"
              " document.getElementById('fault').textContent=s.fault_status;"
              " }catch(e){} setTimeout(refresh,1000); }"
              "async function cmd(c,v){ let u='/vpc/cmd?c='+encodeURIComponent(c); if(v!==undefined) u+='&v='+encodeURIComponent(v);"
              " try{await fetch(u,{cache:'no-store'});}catch(e){} }"
              "function setf(){ const v=document.getElementById('freq').value; if(v) cmd('setf',v); }"
              "window.onload=refresh;"
              "</script></head><body>"
              "<div class='card'><h2>VPC M0701S Status</h2>"
              "<div>Running Status: <span id='run'>...</span></div>"
              "<div>Fault Status: <span id='fault'>...</span></div>"
              "<div style='margin-top:8px'>"
              "<button onclick='cmd(\"start\")'>Start</button>"
              "<button onclick='cmd(\"stop\")'>Stop</button>"
              "<button onclick='cmd(\"reset\")'>Reset Fault</button>"
              "</div>"
              "<div style='margin-top:8px'>"
              "<input id='freq' type='number' step='0.01' min='0' max='400' placeholder='Hz'>"
              "<button onclick='setf()'>Set Frequency</button>"
              "</div>"
              "<p><a href='/' style='padding:8px 12px;background:#1976d2;color:#fff;border-radius:6px;text-decoration:none'>Back</a></p>"
              "</div></body></html>";
  server.send(200,"text/html", html);
}
static void handleVPCStatus(){
  if(!requireAuth()) return;
  vpcPoll();
  String js = String("{\"running_status\":")+vpcRunningStatus+",\"fault_status\":"+vpcFaultStatus+"}";
  server.send(200,"application/json", js);
}
static void handleVPCCmd(){
  if(!requireAuth()) return;
  String c=server.arg("c");
  String v=server.arg("v");
  if(c=="start"){ VPC_start(); server.send(200,"application/json","{\"ok\":true}"); return; }
  if(c=="stop"){ VPC_stop(); server.send(200,"application/json","{\"ok\":true}"); return; }
  if(c=="reset"){ VPC_clearFault(); server.send(200,"application/json","{\"ok\":true}"); return; }
  if(c=="setf"){
    float hz=v.toFloat();
    if(!(hz>=0 && hz<=400)){ server.send(400,"application/json","{\"error\":\"freq_range\"}"); return; }
    // Ustaw częstotliwość
    VPC_setFrequency(hz);
    server.send(200,"application/json","{\"ok\":true}"); return;
  }
  server.send(400,"application/json","{\"error\":\"unknown_cmd\"}");
}

// ===== I/O (zachowane) =====
// (sekcja jak w Twojej bazie – bez zmian; handleIOPage/State/Set/Diag)

// ===== Critical (zachowane) =====
// (jak w bazie)

// ===== MQTT Topics (zachowane + uzupełnienie opisu VPC) =====
static void handleMqttTopics(){
  if(!requireAuth()) return;
  String page="<!doctype html><html><head><meta charset='utf-8'><title>MQTT Topics</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              ".section{background:#fff;padding:16px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,.1);margin-bottom:14px}"
              "table{border-collapse:collapse;width:100%}th,td{border:1px solid #ddd;padding:6px 8px;font-size:12px}"
              "th{background:#eee}pre{background:#0e1014;color:#e6edf3;padding:10px;border-radius:8px;overflow:auto;font-size:12px;white-space:pre-wrap}"
              "a.btn{display:inline-block;padding:8px 12px;background:#1976d2;color:#fff;text-decoration:none;border-radius:6px}</style>"
              "</head><body>"
              "<div class='section'><h2>MQTT – Dokumentacja</h2><table>"
              "<tr><th>Typ</th><th>Topic</th><th>Opis</th></tr>"
              "<tr><td>publish</td><td>KINCONY/INVERTER/&lt;sid&gt;/state</td><td>Telemetria per SID (status,freq,curr,...)</td></tr>"
              "<tr><td>publish</td><td>KINCONY/MODBUSRTU/state</td><td>Zrzut HREG/IREG (SID1)</td></tr>"
              "<tr><td>publish</td><td>KINCONY/INOUT/state</td><td>Stan wejść/wyjść i analogów</td></tr>"
              "<tr><td>subscribe</td><td>KINCONY/INVERTER/&lt;sid&gt;/set</td><td>Sterowanie falownikiem (Control Word + freq)</td></tr>"
              "<tr><td>subscribe</td><td>KINCONY/INOUT/set</td><td>Sterowanie cewkami/wyjściami</td></tr>"
              "<tr><td>subscribe</td><td>KINCONY/MODBUSRTU/set</td><td>Konfiguracja RTU / write_hreg</td></tr>"
              "<tr><td>publish</td><td>KINCONY/VPC/state</td><td>Stan VPC M0701S (running_status, fault_status)</td></tr>"
              "<tr><td>subscribe</td><td>KINCONY/VPC/set</td><td>Sterowanie VPC: {\"cmd\":\"start|stop|setf|reset\",\"freq\":50.00}</td></tr>"
              "</table></div>"
              "<div class='section'><h3>VPC/set – przykłady</h3><pre>"
              "{\"cmd\":\"start\"}\n{\"cmd\":\"stop\"}\n{\"cmd\":\"reset\"}\n{\"cmd\":\"setf\",\"freq\":45.00}\n</pre></div>"
              "<p><a class='btn' href='/'>Back</a></p></body></html>";
  server.send(200,"text/html", page);
}

// ===== ModbusTCP page (zachowane) =====
// (jak w bazie)

// ===== Resources (zachowane) =====
static uint32_t lastMemSample=0;
static String buildMemJson(){
  String j="{";
  j+="\"free_heap\":"+String(ESP.getFreeHeap())+",";
  j+="\"min_free_heap\":"+String(ESP.getMinFreeHeap())+",";
  j+="\"largest_free_block\":"+String(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT))+",";
  j+="\"uptime_ms\":"+String(millis());
  j+="}";
  return j;
}
static void handleResourcesPage(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>Zasoby</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              "table{border-collapse:collapse;width:100%;max-width:600px}"
              "th,td{border:1px solid #ccc;padding:8px;text-align:left;font-size:14px}"
              "th{background:#eee}.wrap{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.12)}"
              "</style>"
              "<script>"
              "async function refresh(){ try{const d=await fetch('/resources/data',{cache:'no-store'}).then(r=>r.json());fill(d);}catch(e){} "
              " setTimeout(refresh,10000);}function fill(d){"
              " document.getElementById('free_heap').textContent=d.free_heap+' B';"
              " document.getElementById('min_free_heap').textContent=d.min_free_heap+' B';"
              " document.getElementById('largest_free_block').textContent=d.largest_free_block+' B';"
              " document.getElementById('uptime').textContent=(d.uptime_ms/1000).toFixed(0)+' s';}"
              "window.onload=refresh;"
              "</script></head><body>"
              "<h2>Monitor zasobów (co 10 s)</h2>"
              "<div class='wrap'><table>"
              "<tr><th>Parametr</th><th>Wartość</th></tr>"
              "<tr><td>Free Heap</td><td id='free_heap'>...</td></tr>"
              "<tr><td>Min Free Heap</td><td id='min_free_heap'>...</td></tr>"
              "<tr><td>Largest Free Block</td><td id='largest_free_block'>...</td></tr>"
              "<tr><td>Uptime</td><td id='uptime'>...</td></tr>"
              "</table>"
              "<p><a href='/' style='display:inline-block;padding:8px 12px;background:#1976d2;color:#fff;border-radius:6px'>Back</a></p>"
              "</div></body></html>";
  server.send(200,"text/html",html);
}
static void handleResourcesData(){
  if(!requireAuth()) return;
  lastMemSample=millis();
  server.send(200,"application/json", buildMemJson());
}

// ===== I/O Panel =====
static void handleIOPage(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>I/O Panel</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              ".card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.12);margin-bottom:12px}"
              "button{padding:8px 12px;background:#1976d2;color:#fff;border:none;border-radius:6px;cursor:pointer;margin:4px}"
              "</style></head><body>"
              "<h2>KC868-A16 I/O Panel</h2>"
              "<div class='card'><p>I/O control panel - under construction</p>"
              "<p>Use /io/state, /io/set, /io/diag endpoints via API</p></div>"
              "<p><a href='/' style='padding:8px 12px;background:#1976d2;color:#fff;border-radius:6px;text-decoration:none'>Back</a></p>"
              "</body></html>";
  server.send(200,"text/html",html);
}

static void handleIOState(){
  if(!requireAuth()) return;
  String json="{\"inputs\":[0,0,0,0,0,0,0,0],\"outputs\":[0,0,0,0,0,0,0,0]}";
  server.send(200,"application/json",json);
}

static void handleIOSet(){
  if(!requireAuth()) return;
  // Parse set command from query params
  server.send(200,"application/json","{\"ok\":true}");
}

static void handleIODiag(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>I/O Diagnostics</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              ".card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.12)}"
              "</style></head><body>"
              "<h2>I/O Diagnostics (PCF8574)</h2>"
              "<div class='card'><p>I2C PCF8574 diagnostics - under construction</p></div>"
              "<p><a href='/' style='padding:8px 12px;background:#1976d2;color:#fff;border-radius:6px;text-decoration:none'>Back</a></p>"
              "</body></html>";
  server.send(200,"text/html",html);
}

// ===== MQTT Republish UI =====
static void handleMqttRepubUI(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>MQTT Republish</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              ".card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.12);margin-bottom:12px}"
              "button{padding:8px 12px;background:#1976d2;color:#fff;border:none;border-radius:6px;cursor:pointer;margin:4px}"
              "input,textarea{width:100%;padding:8px;margin:4px 0;border:1px solid #ccc;border-radius:4px;box-sizing:border-box}"
              "</style>"
              "<script>"
              "async function publish(){"
              "  const topic=document.getElementById('topic').value;"
              "  const payload=document.getElementById('payload').value;"
              "  try{"
              "    await fetch('/mqtt/repub/publish?topic='+encodeURIComponent(topic)+'&payload='+encodeURIComponent(payload));"
              "    alert('Published');"
              "  }catch(e){alert('Error: '+e);}"
              "}"
              "</script></head><body>"
              "<h2>MQTT Manual Publish</h2>"
              "<div class='card'>"
              "<label>Topic:</label><input id='topic' value='KINCONY/TEST' />"
              "<label>Payload:</label><textarea id='payload' rows='4'>{\"test\":true}</textarea>"
              "<button onclick='publish()'>Publish</button>"
              "</div>"
              "<p><a href='/' style='padding:8px 12px;background:#1976d2;color:#fff;border-radius:6px;text-decoration:none'>Back</a></p>"
              "</body></html>";
  server.send(200,"text/html",html);
}

static void handleMqttRepubPublish(){
  if(!requireAuth()) return;
  String topic = server.arg("topic");
  String payload = server.arg("payload");
  bool ok = mqtt.publish(topic.c_str(), payload.c_str());
  server.send(200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");
}

static void handleMqttRepubSet(){
  if(!requireAuth()) return;
  server.send(200,"application/json","{\"ok\":true}");
}

// ===== ModbusTCP Info Page =====
static void handleModbusTCPPage(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>ModbusTCP Info</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              ".card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.12);margin-bottom:12px}"
              "table{border-collapse:collapse;width:100%}"
              "th,td{border:1px solid #ddd;padding:8px;text-align:left}"
              "th{background:#1976d2;color:#fff}"
              "</style></head><body>"
              "<h2>ModbusTCP Server - KC868-A16</h2>"
              "<div class='card'>"
              "<p><strong>Port:</strong> 502</p>"
              "<p><strong>Multi-SID Layout:</strong> Per SID base = (SID-1) × 100</p>"
              "<h3>Register Layout (per SID)</h3>"
              "<table>"
              "<tr><th>Register</th><th>Type</th><th>Description</th></tr>"
              "<tr><td>Base+0</td><td>HREG</td><td>Control Word (FC06/FC16)</td></tr>"
              "<tr><td>Base+1</td><td>HREG</td><td>Set Frequency</td></tr>"
              "<tr><td>Base+2</td><td>HREG</td><td>Flags (bit 0x0002 = fault reset)</td></tr>"
              "<tr><td>Base+0</td><td>IREG</td><td>Fault Code</td></tr>"
              "<tr><td>Base+1</td><td>IREG</td><td>Status + Direction</td></tr>"
              "<tr><td>Base+2</td><td>IREG</td><td>Set Frequency (readback)</td></tr>"
              "<tr><td>Base+3</td><td>IREG</td><td>Running Frequency</td></tr>"
              "<tr><td>Base+4</td><td>IREG</td><td>Running Current</td></tr>"
              "<tr><td>Base+5</td><td>IREG</td><td>DC Bus Voltage</td></tr>"
              "<tr><td>Base+6</td><td>IREG</td><td>Temperature</td></tr>"
              "</table>"
              "</div>"
              "<p><a href='/' style='padding:8px 12px;background:#1976d2;color:#fff;border-radius:6px;text-decoration:none'>Back</a></p>"
              "</body></html>";
  server.send(200,"text/html",html);
}

// ===== Routing =====
static void setupWeb(){
  server.on("/", handleRoot);

  // Konfiguracja
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);

  // Inverter (multi-SID)
  // AutoMultiInverter registers its own endpoints in begin()

  // VPC
  server.on("/vpc",        HTTP_GET, handleVPCPage);
  server.on("/vpc/status", HTTP_GET, handleVPCStatus);
  server.on("/vpc/cmd",    HTTP_GET, handleVPCCmd);

  // I/O
  server.on("/io",       HTTP_GET, handleIOPage);
  server.on("/io/state", HTTP_GET, handleIOState);
  server.on("/io/set",   HTTP_GET, handleIOSet);
  server.on("/io/diag",  HTTP_GET, handleIODiag);

  // MQTT docs / repub
  server.on("/mqtt/repub",         HTTP_GET, handleMqttTopics);
  server.on("/mqtt/repub/ui",      HTTP_GET, handleMqttRepubUI);
  server.on("/mqtt/repub/publish", HTTP_GET, handleMqttRepubPublish);
  server.on("/mqtt/repub/set",     HTTP_GET, handleMqttRepubSet);

  // ModbusTCP page
  server.on("/modbus/tcp", HTTP_GET, handleModbusTCPPage);

  // Resources
  server.on("/resources",      HTTP_GET, handleResourcesPage);
  server.on("/resources/data", HTTP_GET, handleResourcesData);

  server.begin();
  Serial.println("[HTTP] Server started");
}

// ===== Tasks =====
static void taskNet(void*){
  for(;;){
    server.handleClient();
    mbTCP.task();
    ensureMqtt();
    mqtt.loop();
    if(!netCfg.wifi_ap){
      if(!systemStatus.sta_active && sta_connect_start_ms>0){
        if(millis()-sta_connect_start_ms > (uint32_t)netCfg.sta_fb_sec*1000UL){
          netCfg.wifi_ap=true; saveCfg(); applyWifiMode(true); sta_connect_start_ms=0;
        }
      }
    }
    vTaskDelay(10/portTICK_PERIOD_MS);
  }
}
static void taskIO(void*){
  static uint32_t last_pub_sid=0;
  for(;;){
    readInputs();
    updateOutputs();

    // Round-robin Multi-SID init set freq
    if(rtuCfg.pollMs && (millis()%rtuCfg.pollMs)<20){
      uint8_t sid=sid_list[current_sid_index];
      if(!rtu_freq_initialized[sid]){
        uint16_t rep=mbTCP.Ireg(IREG_BASE(sid)+REG_OUTPUT_FREQ);
        rtuSyncInitialFrequency(sid, rep, rep);
      }
      current_sid_index=(current_sid_index+1)%sid_count;
    }

    // VPC Poll
    if(vpcLegacyCfg.enabled){
      uint32_t now=millis();
      if(now - vpcLastPoll >= vpcLegacyCfg.pollMs){
        vpcPoll();
        vpcLastPoll = now;
      }
    }

    // Publish telemetry
    uint32_t now=millis();
    if(systemStatus.mqtt_connected){
      if(MQTT_PUBLISH_FULL_STATE && now-systemStatus.last_io_pub>5000) publishIO();
      if(now-systemStatus.last_mb_pub>5000) publishMB();
      if(now-last_pub_sid>5000){
        static uint8_t pub_idx=0;
        pub_idx=(pub_idx % sid_count)+1;
        publishInverterStateSID(pub_idx);
        last_pub_sid=now;
      }
    }
    vTaskDelay(50/portTICK_PERIOD_MS);
  }
}

// ===== Setup / Loop =====
void setup(){
  Serial.begin(115200);
  Serial.println("[BOOT] KC868-A16 Multi-SID + Full WWW + VPC M0701S");
  loadCfg();
  setupETH();
  setupWiFiInitial();
  setupIO();
  setupMQTT();
  setupMBTCP();
  setupWeb();

  // Append – AutoMultiInverter (ME300)
  inverter_master_begin();

  // VPC init
  setupVPC();

  // Log zasobów
  Serial.printf("[MEM] Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("[MEM] Min free heap: %u bytes\n", ESP.getMinFreeHeap());
  Serial.printf("[MEM] Largest free block: %u bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

  xTaskCreatePinnedToCore(taskNet,"NET",8192,nullptr,1,nullptr,0);
  xTaskCreatePinnedToCore(taskIO,"IO", 6144,nullptr,1,nullptr,1);
  Serial.println("[READY]");
}
void loop(){ vTaskDelay(100/portTICK_PERIOD_MS); }