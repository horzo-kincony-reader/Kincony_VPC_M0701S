/*
  KC868-A16 MASTER – Multi SID v21a (Version2_Version22)
  Pełny plik – pełna funkcjonalna zawartość WWW bez stubów i bez śmieciowych informacji.
  Zawiera:
    - Multi-SID ModbusTCP (6 falowników, offset 100 HREG/IREG per SID)
    - MQTT (INVERTER/<sid>/set, INOUT/set, MODBUSRTU/set)
    - WWW:
      / (root) – nawigacja
      /config (GET/POST) – konfiguracja ETH, WiFi AP/STA, RTU, TCP
      /inverter – panel multi-SID z kontrolą Control Word i częstotliwości
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
      /resources – Zasoby (monitor pamięci, odświeżanie co 10 s)  // DODANE
      /resources/data – JSON z zasobami                              // DODANE
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
#include <esp_heap_caps.h>  // DODANE: monitor pamięci (heap_caps_get_largest_free_block)

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

// Multi-SID runtime
static bool     rtu_freq_initialized[MAX_SID+1]={false};
static uint16_t rtu_last_known_setf[MAX_SID+1]={0};
static uint8_t  sid_list[MAX_SID]={1,2,3,4,5,6};
static uint8_t  sid_count=6;
static uint8_t  current_sid_index=0;

// Append moduł (extern "C")
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

  prefs.begin("invrtu", true);
  rtuCfg.baud   = prefs.getULong("baud",9600);
  rtuCfg.parity = prefs.getUChar("par",0);
  rtuCfg.pollMs = prefs.getUShort("poll",500);
  prefs.end();
  rtuCfg.pollMs=constrain((int)rtuCfg.pollMs,100,5000);
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

// Guard częstotliwości
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

// MQTT (minimal – z pełnym handlerem INVERTER/<sid>/set)
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
      }
      if(msg.indexOf("\"write_hreg\"")!=-1){
        int pos=0;
        while(true){
          int aPos=msg.indexOf("\"addr\"",pos);
          int vPos=msg.indexOf("\"value\"",pos);
          if(aPos==-1 || vPos==-1) break;
          auto extractIntAfter=[&](int p)->long{
            int colon=msg.indexOf(":",p); if(colon==-1) return -1L;
            int s=colon+1; while(s<(int)msg.length() && msg[s]==' ') s++;
            int e=s; while(e<(int)msg.length() && (isDigit(msg[e])||msg[e]=='x'||isxdigit(msg[e]))) e++;
            String tok=msg.substring(s,e);
            if(tok.startsWith("0x")||tok.startsWith("0X")) return (long) strtol(tok.c_str(),nullptr,16);
            return (long)tok.toInt();
          };
          long addr=extractIntAfter(aPos);
          long value=extractIntAfter(vPos);
          // dla prostoty: zapis do SID1 lokalnego HREG od 0x2000
          int hindex = (int)(addr - 8192); // 0x2000
          if(hindex>=0 && hindex<100){
            mbTCP.Hreg(HREG_BASE(1) + hindex, (uint16_t)value);
          }
          pos=vPos+7;
        }
      }
      return;
    }

    // INVERTER/<sid>/set
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
              "<a class='btn' href='/inverter'>Inverter Multi-SID</a>"
              "<a class='btn' href='/io'>I/O Panel</a>"
              "<a class='btn' href='/io/diag'>I/O Diagnostics</a>"
              "<a class='btn' href='/mqtt/repub'>MQTT Topics</a>"
              "<a class='btn' href='/mqtt/repub/ui'>MQTT Republish</a>"
              "<a class='btn' href='/modbus/tcp'>ModbusTCP</a>"
              "<a class='btn' href='/critical'>Critical</a>"
              "<a class='btn' href='/resources'>Zasoby</a>"  // DODANE link do zakładki Zasoby
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
             "<fieldset><legend>Modbus RTU (Global)</legend>"
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
             "<label>Poll [ms]</label><input type='text' name='rtu_poll' value='%RTU_POLL%'>"
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
  tcpCfg.enabled = server.arg("tcp_en")=="1";
  { int p=server.arg("tcp_port").toInt(); if(p>=1&&p<=65535) tcpCfg.port=p; }
  saveCfg();
  inverter_rtu_apply(0, rtuCfg.baud, rtuCfg.parity, rtuCfg.pollMs);
  applyWifiMode(netCfg.wifi_ap);
  server.send(200,"application/json","{\"ok\":true}");
}

// ===== Inverter (Multi-SID pełny panel) =====
static void handleInverterState(){ // kompat SID1
  int ib=IREG_BASE(1);
  String j="{\"status\":";
  j+=String(mbTCP.Ireg(ib+REG_STATUS_WORD));
  j+=",\"freq\":";
  j+=String(mbTCP.Ireg(ib+REG_OUTPUT_FREQ));
  j+=",\"curr\":";
  j+=String(mbTCP.Ireg(ib+REG_OUTPUT_CURRENT));
  j+=",\"volt\":";
  j+=String(mbTCP.Ireg(ib+REG_OUTPUT_VOLTAGE));
  j+=",\"power\":";
  j+=String(mbTCP.Ireg(ib+REG_OUTPUT_POWER));
  j+=",\"dcv\":";
  j+=String(mbTCP.Ireg(ib+REG_DC_BUS_VOLTAGE));
  j+=",\"rpm\":";
  j+=String(mbTCP.Ireg(ib+REG_RPM));
  j+="}";
  server.send(200,"application/json",j);
}
static void handleInverterStateAll(){
  String j="[";
  for(uint8_t sid=1; sid<=sid_count; sid++){
    int ib=IREG_BASE(sid);
    j+="{\"sid\":"+String(sid)+
       ",\"status\":"+String(mbTCP.Ireg(ib+REG_STATUS_WORD))+
       ",\"freq\":"+String(mbTCP.Ireg(ib+REG_OUTPUT_FREQ))+
       ",\"curr\":"+String(mbTCP.Ireg(ib+REG_OUTPUT_CURRENT))+
       ",\"volt\":"+String(mbTCP.Ireg(ib+REG_OUTPUT_VOLTAGE))+
       ",\"power\":"+String(mbTCP.Ireg(ib+REG_OUTPUT_POWER))+
       ",\"dcv\":"+String(mbTCP.Ireg(ib+REG_DC_BUS_VOLTAGE))+
       ",\"rpm\":"+String(mbTCP.Ireg(ib+REG_RPM))+"}";
    if(sid<sid_count) j+=",";
  }
  j+="]";
  server.send(200,"application/json",j);
}
static void handleInverterPage(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>Inverter Multi-SID</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>"
              "body{font-family:Arial;margin:16px;background:#f5f6fa;color:#222}"
              "table{border-collapse:collapse;width:100%;margin-bottom:14px}"
              "th,td{border:1px solid #d0d6e2;padding:6px 8px;font-size:12px;text-align:center}"
              "th{background:#e9eef5}"
              ".ctrl-panel{display:flex;flex-wrap:wrap;gap:12px;margin-bottom:14px}"
              ".box{background:#fff;padding:12px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,.08);flex:1;min-width:300px}"
              "input,select{padding:4px 6px;font-size:12px;margin:4px 0;width:100%}"
              "button{padding:6px 10px;font-size:12px;background:#1976d2;color:#fff;border:none;border-radius:6px;cursor:pointer;margin:3px}"
              ".status-run{background:#c8f7c5}.status-stop{background:#ffd1d1}"
              ".small{font-size:11px;color:#666}"
              "</style>"
              "<script>"
              "let lastData=[];"
              "async function refresh(){"
              " try{const r=await fetch('/inverter/state_all',{cache:'no-store'});"
              " if(!r.ok)return; const arr=await r.json(); lastData=arr; renderTable(arr); }catch(e){console.error(e);} "
              " setTimeout(refresh,1000);"
              "}"
              "function renderTable(arr){"
              " const tbody=document.getElementById('tbody'); if(!tbody)return; tbody.innerHTML='';"
              " arr.forEach(o=>{"
              "  const tr=document.createElement('tr');"
              "  const run=(o.status&0x0001)!==0; tr.className=run?'status-run':'status-stop';"
              "  tr.innerHTML=`<td>${o.sid}</td>"
              "    <td>0x${o.status.toString(16).padStart(4,'0')}</td>"
              "    <td>${(o.freq/100).toFixed(2)}</td>"
              "    <td>${(o.curr/100).toFixed(2)}</td>"
              "    <td>${(o.volt/10).toFixed(1)}</td>"
              "    <td>${(o.power/10).toFixed(1)}</td>"
              "    <td>${(o.dcv/10).toFixed(1)}</td>"
              "    <td>${o.rpm}</td>"
              "    <td>"
              "      <button onclick='sendCmd(${o.sid},\"start\")'>Start</button>"
              "      <button onclick='sendCmd(${o.sid},\"stop\")'>Stop</button>"
              "      <button onclick='sendCmd(${o.sid},\"reset\")'>Reset</button>"
              "      <button onclick='sendCmd(${o.sid},\"jog\")'>Jog</button>"
              "    </td>`;"
              "  tbody.appendChild(tr);"
              " });"
              "}"
              "async function sendCmd(sid,cmd,v){"
              " let url='/inverter/cmd?sid='+sid+'&c='+cmd; if(v!==undefined) url+='&v='+encodeURIComponent(v);"
              " try{await fetch(url,{cache:'no-store'});}catch(e){console.error(e);} }"
              "function applyAdvanced(){"
              " const sid=parseInt(document.getElementById('adv_sid').value||'1');"
              " const dir=document.getElementById('adv_dir').value;"
              " const freq=document.getElementById('adv_freq').value;"
              " const acc=document.getElementById('adv_acc').value;"
              " const preset=document.getElementById('adv_preset').value;"
              " const pen=document.getElementById('adv_preset_en').checked?1:0;"
              " const cm=document.getElementById('adv_cm').value;"
              " if(dir) sendCmd(sid,'dir',dir);"
              " if(freq) sendCmd(sid,'freq',freq);"
              " if(acc) sendCmd(sid,'acc_set',acc);"
              " if(preset) sendCmd(sid,'preset',preset);"
              " sendCmd(sid,'preset_enable',pen);"
              " if(cm) sendCmd(sid,'control_mode',cm);"
              "}"
              "window.onload=refresh;"
              "</script>"
              "</head><body><h2>Inverter Multi-SID Panel</h2>"
              "<div class='box'>"
              "<div class='small'>Control Word wg rejestry_ME300_Version2.csv: bity 1..0 RUN/STOP/JOG, 5..4 kierunek, 7..6 acc/dec set, 11..8 preset, 12 enable preset/time, 14..13 control mode.</div>"
              "<table><thead><tr><th>SID</th><th>StatusWord</th><th>Freq(Hz)</th><th>Curr(A)</th><th>Volt(V)</th><th>Power(kW)</th><th>DCV(V)</th><th>RPM</th><th>Basic Ctrl</th></tr></thead><tbody id='tbody'></tbody></table>"
              "</div>"
              "<div class='ctrl-panel'>"
              "<div class='box'><h3>Advanced Control</h3>"
              "<label>SID<select id='adv_sid'>";
  for(uint8_t sid=1; sid<=sid_count; sid++) html+="<option value='"+String(sid)+"'>"+String(sid)+"</option>";
  html+="</select></label>"
       "<label>Direction<select id='adv_dir'><option value=''>--</option><option value='fwd'>FWD</option><option value='rev'>REV</option><option value='chg'>CHANGE</option></select></label>"
       "<label>Frequency (Hz)<input id='adv_freq' type='number' step='0.01' min='0' max='400' placeholder='e.g. 50.00'></label>"
       "<label>Acc/Dec Set<select id='adv_acc'><option value=''>--</option><option value='0'>0</option><option value='1'>1</option><option value='2'>2</option><option value='3'>3</option></select></label>"
       "<label>Preset<select id='adv_preset'><option value=''>--</option>";
  for(int p=0;p<=15;p++) html+="<option value='"+String(p)+"'>"+String(p)+"</option>";
  html+="</select></label>"
       "<label><input type='checkbox' id='adv_preset_en'> Enable Preset/Time</label>"
       "<label>Control Mode<select id='adv_cm'><option value=''>--</option><option value='panel'>panel</option><option value='param'>param</option><option value='change'>change</option></select></label>"
       "<button onclick='applyAdvanced()'>Apply Advanced</button>"
       "</div>"
       "<div class='box'><h3>Control Word bits</h3><pre>"
       "Bits 1..0 : 00 none, 01 STOP, 10 RUN, 11 JOG+RUN\n"
       "Bits 5..4 : 01 FWD, 10 REV, 11 Change dir\n"
       "Bits 7..6 : Acc/Dec set (0..3)\n"
       "Bits 11..8: Preset freq select (0..15)\n"
       "Bit 12    : Enable preset/time set\n"
       "Bits 14..13: Control mode (01 panel, 10 param, 11 change)\n"
       "</pre></div>"
       "</div>"
       "<p><a href='/' style='padding:8px 12px;background:#1976d2;color:#fff;text-decoration:none;border-radius:6px'>Back</a></p>"
       "</body></html>";
  server.send(200,"text/html",html);
}
static void handleInverterCmd(){
  if(!requireAuth()) return;
  uint8_t sid = server.hasArg("sid") ? (uint8_t)server.arg("sid").toInt() : 1;
  if(sid<1 || sid>sid_count){ server.send(400,"application/json","{\"error\":\"sid_range\"}"); return; }
  String c=server.arg("c");
  String v=server.arg("v");
  int hbase=HREG_BASE(sid);
  uint16_t ctrl=mbTCP.Hreg(hbase+REG_CONTROL_WORD);
  auto setBits=[&](int f,int t,uint16_t val){
    uint16_t mask=0; for(int b=f;b<=t;b++) mask|=(1u<<b);
    ctrl=(ctrl & ~mask) | ((val<<f)&mask);
  };

  bool changed=false;
  if(c=="start"){ setBits(0,1,0b10); changed=true; }
  else if(c=="stop"){ setBits(0,1,0b01); changed=true; }
  else if(c=="reset"){ ctrl |= 0x0004; changed=true; }
  else if(c=="jog"){ setBits(0,1,0b11); changed=true; }
  else if(c=="dir"){
    if(v=="fwd"){ setBits(4,5,0b01); changed=true; }
    else if(v=="rev"){ setBits(4,5,0b10); changed=true; }
    else if(v=="chg"){ setBits(4,5,0b11); changed=true; }
  } else if(c=="acc_set"){
    int acc=v.toInt(); if(acc>=0 && acc<=3){ setBits(6,7,(uint16_t)acc); changed=true; }
  } else if(c=="preset"){
    int preset=v.toInt(); if(preset>=0 && preset<=15){ setBits(8,11,(uint16_t)preset); changed=true; }
  } else if(c=="preset_enable"){
    int pen=v.toInt(); setBits(12,12, pen?1:0); changed=true;
  } else if(c=="control_mode"){
    if(v=="panel") setBits(13,14,0b01), changed=true;
    else if(v=="param") setBits(13,14,0b10), changed=true;
    else if(v=="change") setBits(13,14,0b11), changed=true;
  } else if(c=="freq"){
    float hz=v.toFloat();
    if(hz>=0 && hz<=400){
      uint16_t setf=(uint16_t)round(hz*100.0f);
      if(rtu_freq_initialized[sid] || setf>0){
        mbTCP.Hreg(hbase+REG_FREQUENCY_SET,setf);
      }
      server.send(200,"application/json","{\"ok\":true,\"sid\":"+String(sid)+",\"freq_set\":"+String(hz,2)+"}");
      return;
    } else { server.send(400,"application/json","{\"error\":\"freq_range\"}"); return; }
  } else { server.send(400,"application/json","{\"error\":\"unknown_cmd\"}"); return; }

  if(changed) mbTCP.Hreg(hbase+REG_CONTROL_WORD, ctrl);
  server.send(200,"application/json","{\"ok\":true,\"sid\":"+String(sid)+",\"control_word\":"+String(ctrl)+"}");
}

// ===== I/O (zachowane) =====
static void handleIOPage(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>I/O Panel</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}"
              ".card{background:#fff;padding:14px;border-radius:10px;box-shadow:0 2px 6px rgba(0,0,0,.1)}"
              ".chip{display:inline-block;padding:4px 8px;border-radius:6px;margin:2px;font-size:12px;border:1px solid #999}"
              ".on{background:#c8f7c5;border-color:#27ae60}.off{background:#ffd1d1;border-color:#c0392b}"
              ".btn{padding:6px 10px;background:#1976d2;color:#fff;border:none;border-radius:6px;cursor:pointer;font-size:12px;margin:3px}"
              "</style><script>"
              "async function loadIO(){"
              " const s=await fetch('/io/state',{credentials:'include'}).then(r=>r.json());"
              " let di=document.getElementById('di'); di.innerHTML='';"
              " for(let i=0;i<16;i++){ const v=s.di[i]; di.innerHTML+=`<span class='chip ${v?'on':'off'}'>DI${String(i+1).padStart(2,'0')}:${v}</span>`; }"
              " let ai=document.getElementById('ai'); ai.innerHTML='';"
              " for(let i=0;i<4;i++){ ai.innerHTML+=`<div>AI${i+1}: ${s.ai[i]}</div>`; }"
              " let dOut=document.getElementById('do'); dOut.innerHTML='';"
              " for(let i=0;i<16;i++){ const v=s.do[i]; dOut.innerHTML+=`<button class='btn' onclick='tog(${i})'>Y${String(i+1).padStart(2,'0')} => ${v?'ON':'OFF'}</button>`; }"
              " setTimeout(loadIO,1000);}"
              "async function tog(ch){ await fetch('/io/set?ch='+ch+'&v=toggle',{credentials:'include'}); }"
              "window.onload=loadIO;"
              "</script></head><body><h2>I/O Panel</h2>"
              "<div class='grid'>"
              "<div class='card'><h3>Digital Inputs</h3><div id='di'></div></div>"
              "<div class='card'><h3>Analog Inputs</h3><div id='ai'></div></div>"
              "<div class='card'><h3>Digital Outputs</h3><div id='do'></div></div>"
              "</div><p><a href='/'>Back</a></p></body></html>";
  server.send(200,"text/html", html);
}
static void handleIOState(){
  String j="{\"di\":[";
  for(int i=0;i<16;i++){ j+=systemStatus.di[i]; if(i<15) j+=","; }
  j+="],\"ai\":[";
  for(int i=0;i<4;i++){ j+=systemStatus.ai[i]; if(i<3) j+=","; }
  j+="],\"do\":[";
  for(int i=0;i<16;i++){ j+=systemStatus.dout[i]; if(i<15) j+=","; }
  j+="]}";
  server.send(200,"application/json", j);
}
static void handleIOSet(){
  if(!requireAuth()) return;
  int ch=server.arg("ch").toInt(); String v=server.arg("v");
  if(ch>=0 && ch<16){
    bool cur=systemStatus.dout[ch];
    bool nv=(v=="toggle")? !cur : (v=="1");
    mbTCP.Coil(ch,nv);
    systemStatus.dout[ch]=nv?1:0;
  }
  server.send(200,"application/json","{\"ok\":true}");
}

// ===== I/O Diagnostics =====
static void handleIODiag(){
  if(!requireAuth()) return;
  uint8_t in1_bits=0xFF,in2_bits=0xFF,out1_bits=0xFF,out2_bits=0xFF;
  if(has_IN1){ for(int i=0;i<8;i++){ uint8_t v=pcf_IN1.digitalRead(i); if(v) in1_bits|=(1<<i); else in1_bits&=~(1<<i);} }
  if(has_IN2){ for(int i=0;i<8;i++){ uint8_t v=pcf_IN2.digitalRead(i); if(v) in2_bits|=(1<<i); else in2_bits&=~(1<<i);} }
  if(has_OUT1){ for(int i=0;i<8;i++){ if(systemStatus.dout[i]) out1_bits&=~(1<<i); else out1_bits|=(1<<i);} }
  if(has_OUT2){ for(int i=0;i<8;i++){ if(systemStatus.dout[i+8]) out2_bits&=~(1<<i); else out2_bits|=(1<<i);} }
  int maxOut=16;
  String j;
  j.reserve(900);
  j="{\"i2c_present\":{\"OUT1\":";
  j+=(has_OUT1?"true":"false");
  j+=",\"OUT2\":";
  j+=(has_OUT2?"true":"false");
  j+=",\"IN1\":";
  j+=(has_IN1?"true":"false");
  j+=",\"IN2\":";
  j+=(has_IN2?"true":"false");
  j+="},\"pcf_raw\":{\"IN1_bits\":\"0b";
  for(int i=7;i>=0;i--) j+=(in1_bits&(1<<i))?'1':'0';
  j+="\",\"IN2_bits\":\"0b";
  for(int i=7;i>=0;i--) j+=(in2_bits&(1<<i))?'1':'0';
  j+="\",\"OUT1_shadow\":\"0b";
  for(int i=7;i>=0;i--) j+=(out1_bits&(1<<i))?'1':'0';
  j+="\",\"OUT2_shadow\":\"0b";
  for(int i=7;i>=0;i--) j+=(out2_bits&(1<<i))?'1':'0';
  j+="\"},\"map\":{\"coils\":[";
  for(int i=0;i<maxOut;i++){ j+=(mbTCP.Coil(i)?"1":"0"); if(i<maxOut-1) j+=","; }
  j+="],\"digital_outputs\":[";
  for(int i=0;i<maxOut;i++){ j+=String(systemStatus.dout[i]); if(i<maxOut-1) j+=","; }
  j+="],\"digital_inputs\":[";
  for(int i=0;i<16;i++){ j+=String(systemStatus.di[i]); if(i<15) j+=","; }
  j+="]},\"notes\":\"INx_bits: 1=HIGH(pull-up), 0=LOW(aktywny). OUTx_shadow: 0=LOW(ON), 1=HIGH(OFF).\"}";
  String html="<!doctype html><html><head><meta charset='utf-8'><title>I/O Diagnostics</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:18px;background:#f5f7fb;color:#222}.card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.08);margin-bottom:14px}"
              "code,pre{background:#0e1014;color:#e6edf3;padding:10px;border-radius:8px;overflow:auto}</style></head><body>"
              "<h1>I/O Diagnostics</h1><div class='card'><h3>Presence & Raw</h3><pre id='jsonbox'></pre>"
              "<script>const data="+j+";document.getElementById('jsonbox').textContent=JSON.stringify(data,null,2);</script>"
              "<p><a href='/'>Back</a></p></div></body></html>";
  server.send(200,"text/html", html);
}

// ===== Critical (JSON w tabeli) =====
static void handleCritical(){
  if(!requireAuth()) return;
  String j="{\"eth\":"+String(systemStatus.eth_connected?"true":"false")+
    ",\"ap\":"+String(systemStatus.ap_active?"true":"false")+
    ",\"sta\":"+String(systemStatus.sta_active?"true":"false")+
    ",\"mqtt\":"+String(systemStatus.mqtt_connected?"true":"false")+
    ",\"i2c\":"+String(systemStatus.i2c_initialized?"true":"false")+
    ",\"ip_eth\":\""+ETH.localIP().toString()+"\""+
    ",\"ip_ap\":\""+(systemStatus.ap_active?WiFi.softAPIP().toString():"")+"\""+
    ",\"ip_sta\":\""+(systemStatus.sta_active?WiFi.localIP().toString():"")+"\""+
    ",\"tcp_enabled\":"+String(tcpCfg.enabled?"true":"false")+
    ",\"tcp_port\":"+String(tcpCfg.port)+
    "}";
  String html="<!doctype html><html><head><meta charset='utf-8'><title>Critical</title>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              "table{border-collapse:collapse;width:100%;max-width:800px}th,td{border:1px solid #ddd;padding:8px;text-align:left}"
              "th{background:#eee}</style></head><body><h2>Critical (System JSON)</h2>"
              "<table id='t'><tr><th>Key</th><th>Value</th></tr></table>"
              "<script>"
              "const d="+j+";"
              "const tbl=document.getElementById('t');"
              "Object.keys(d).forEach(k=>{const tr=document.createElement('tr');"
              " const td1=document.createElement('td'); td1.textContent=k;"
              " const td2=document.createElement('td'); td2.textContent=d[k];"
              " tr.appendChild(td1); tr.appendChild(td2); tbl.appendChild(tr);});"
              "</script><p><a href='/'>Back</a></p></body></html>";
  server.send(200,"text/html", html);
}

// ===== MQTT Topics =====
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
              "</table></div>"
              "<div class='section'><h3>INVERTER/&lt;sid&gt;/set – przykłady</h3><pre>"
              "{\"start\":true}\n{\"stop\":true}\n{\"jog\":true}\n"
              "{\"dir\":\"fwd\"}\n{\"dir\":\"rev\"}\n{\"acc_set\":1}\n"
              "{\"preset\":5,\"preset_enable\":true}\n{\"control_mode\":\"panel\"}\n"
              "{\"freq\":45.00}\n</pre></div>"
              "<div class='section'><h3>MODBUSRTU/set – przykłady</h3><pre>"
              "{\"rtu\":{\"sid\":1,\"baud\":19200,\"par\":\"8E1\",\"poll\":400}}\n"
              "{\"write_hreg\":[{\"addr\":8192,\"value\":2}]}\n"
              "{\"write_hreg\":[{\"addr\":8192,\"value\":1},{\"addr\":8193,\"value\":5000}]}\n"
              "</pre></div>"
              "<div class='section'><h3>INOUT/set – przykłady</h3><pre>"
              "{\"coil\":{\"index\":0,\"value\":1}}\n"
              "{\"coils\":[{\"index\":0,\"value\":1},{\"index\":7,\"value\":0}]}\n"
              "{\"outputs\":[1,0,1,0,0,0,1,0,1,1,0,0,0,0,1,1]}\n"
              "</pre></div>"
              "<p><a class='btn' href='/'>Back</a></p></body></html>";
  server.send(200,"text/html", page);
}

// ===== MQTT Republish UI =====
static void handleMqttRepubUI(){
  if(!requireAuth()) return;
  String html="<!DOCTYPE html><html><head><meta charset='utf-8'><title>MQTT Republish</title>"
              "<style>body{font-family:Arial;margin:20px;background:#f5f5f5}"
              ".card{background:#fff;padding:14px;border-radius:10px;box-shadow:0 2px 6px rgba(0,0,0,.1);margin-bottom:12px}"
              "a.btn,button.btn{padding:8px 12px;background:#1976d2;color:#fff;text-decoration:none;border:none;border-radius:6px;cursor:pointer;margin:4px}"
              "input{padding:6px;width:320px}</style></head><body>"
              "<h2>MQTT Republish</h2>"
              "<div class='card'><h3>Automatyczne topiki</h3>"
              "<a class='btn' href='/mqtt/repub/publish?topic=inout'>Republish INOUT</a>"
              "<a class='btn' href='/mqtt/repub/publish?topic=modbus'>Republish MODBUS</a>"
              "<a class='btn' href='/mqtt/repub/publish?topic=inverter'>Republish INVERTER (all SID)</a>"
              "</div>"
              "<div class='card'><h3>Ręczny publish</h3>"
              "<form method='POST' action='/mqtt/repub/set'>"
              "<label>Topic<br><input name='topic' placeholder='KINCONY/INVERTER/1/set'></label><br>"
              "<label>Payload<br><input name='payload' placeholder='{\"start\":true}'></label><br>"
              "<button class='btn' type='submit'>Publish</button>"
              "</form></div>"
              "<p><a class='btn' href='/'>Back</a></p>"
              "</body></html>";
  server.send(200,"text/html", html);
}
static void handleMqttRepubPublish(){
  if(!requireAuth()) return;
  String key=server.arg("topic");
  if(key=="inout"){ publishIO(); server.send(200,"application/json","{\"ok\":\"inout_republished\"}"); return; }
  if(key=="modbus"){ publishMB(); server.send(200,"application/json","{\"ok\":\"modbus_republished\"}"); return; }
  if(key=="inverter"){
    for(uint8_t sid=1; sid<=sid_count; sid++) publishInverterStateSID(sid);
    server.send(200,"application/json","{\"ok\":\"inverter_republished_all_sid\"}"); return;
  }
  server.send(400,"application/json","{\"error\":\"unknown_key\"}");
}
static void handleMqttRepubSetPublish(){
  if(!requireAuth()) return;
  String topic=server.arg("topic");
  String payload=server.arg("payload");
  if(!topic.length()){ server.send(400,"application/json","{\"error\":\"missing_topic\"}"); return; }
  bool ok=mqtt.publish(topic.c_str(), payload.c_str());
  server.send(ok?200:500,"application/json", ok?"{\"ok\":\"published\"}":"{\"error\":\"publish_failed\"}");
}

// ===== ModbusTCP (rozszerzony) =====
static void handleModbusTCPPage(){
  if(!requireAuth()) return;
  String html="<!doctype html><html><head><meta charset='utf-8'><title>ModbusTCP</title>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<style>body{font-family:Arial;margin:18px;background:#f5f7fb;color:#222}"
              ".card{background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.08);margin-bottom:14px}"
              "table{width:100%;border-collapse:collapse}th,td{padding:8px 10px;border-bottom:1px solid #e3e7ef;text-align:left;font-size:13px}"
              "pre{background:#0e1014;color:#e6edf3;padding:10px;border-radius:8px;overflow:auto;white-space:pre-wrap;font-size:12px}"
              "</style></head><body><h1>Modbus TCP – tryb pracy, polecenia i diagnostyka</h1>";
  html+="<div class='card'><h3>Cechy bieżącego trybu</h3><ul>"
       "<li>Serwer: emelianov ModbusIP, port: "+String(tcpCfg.port)+", enabled: "+String(tcpCfg.enabled?"TAK":"NIE")+"</li>"
       "<li>Multi-SID: 6 falowników, offset 100 rejestrów na SID (HREG/IREG).</li>"
       "<li>COIL/ISTS: globalne dla I/O KC868-A16 (active-low przekaźniki).</li>"
       "<li>Skalowanie: Set/Out Freq 0.01Hz; Voltage/DCBus 0.1V; Current 0.01A (≤655.35A).</li></ul></div>";
  html+="<div class='card'><h3>Adresacja i polecenia klienta</h3><table>"
       "<tr><th>FC</th><th>Zakres</th><th>Opis</th><th>Uwagi</th></tr>"
       "<tr><td>01</td><td>COIL[0..63]</td><td>Odczyt cewek (wyjścia)</td><td>Y01..Y16 (active-low)</td></tr>"
       "<tr><td>02</td><td>ISTS[0..63]</td><td>Odczyt wejść dyskretnych</td><td>DI01..DI16</td></tr>"
       "<tr><td>03</td><td>H(sid)..H(sid)+99</td><td>Odczyt HREG</td><td>H(sid)=(sid-1)*100</td></tr>"
       "<tr><td>04</td><td>I(sid)..I(sid)+99</td><td>Odczyt IREG</td><td>I(sid)=(sid-1)*100</td></tr>"
       "<tr><td>05</td><td>COIL[0..63]</td><td>Zapis pojedynczej cewki</td><td>FF00=ON, 0000=OFF</td></tr>"
       "<tr><td>06</td><td>H(sid)..H(sid)+99</td><td>Zapis pojedynczego HREG</td><td>SetFreq: H(sid)+1 (0.01Hz)</td></tr>"
       "</table></div>";
  html+="<div class='card'><h3>Control Word (0x2000) – szczegóły bitów</h3><pre>"
       "Bit 1~0: 00: Brak; 01: STOP; 10: RUN; 11: JOG+RUN\n"
       "Bit 3~2: Zarezerwowane\n"
       "Bit 5~4: 00: Brak; 01: FWD; 10: REV; 11: Zmiana kierunku\n"
       "Bit 7~6: Wybór zestawu czasów przysp./zwalniania (00..11)\n"
       "Bits 11~8: Wybór częstotliwości (0000 główna, 0001..1111 = preset 1..15)\n"
       "Bit 12: 1 = Załączenie funkcji z bitów 11~6\n"
       "Bits 14~13: Tryb sterowania (00: none, 01: panel, 10: param 00-21, 11: change)\n"
       "Bit 15: Zarezerwowany\n"
       "Mapowanie HREG: H(sid)+0 = Control Word, H(sid)+1 = Set Freq (0.01 Hz)\n"
       "</pre></div>";
  html+="<div class='card'><h3>Przykładowe ramki Modbus TCP (hex)</h3><pre>"
       "FC05 COIL[0]=ON (Y01):\n"
       "MBAP: 00 01 00 00 00 06 01\nPDU : 05 00 00 FF 00\n"
       "Full: 00 01 00 00 00 06 01 05 00 00 FF 00\n\n"
       "FC06 HREG SetFreq SID2=50.00Hz (addr H(2)+1=101=0x0065, val 5000=0x1388):\n"
       "MBAP: 00 02 00 00 00 06 01\nPDU : 06 00 65 13 88\n"
       "Full: 00 02 00 00 00 06 01 06 00 65 13 88\n"
       "</pre></div>";
  html+="<div class='card'><h3>Snapshot per SID</h3><table><tr><th>SID</th><th>Status</th><th>Freq(0.01Hz)</th><th>Curr</th><th>Volt(0.1V)</th><th>Power</th><th>DCV(0.1V)</th><th>RPM</th></tr>";
  for(uint8_t sid=1; sid<=sid_count; sid++){
    int ib=IREG_BASE(sid);
    html+="<tr><td>"+String(sid)+"</td>"
          "<td>0x"+String(mbTCP.Ireg(ib+REG_STATUS_WORD),HEX)+"</td>"
          "<td>"+String(mbTCP.Ireg(ib+REG_OUTPUT_FREQ))+"</td>"
          "<td>"+String(mbTCP.Ireg(ib+REG_OUTPUT_CURRENT))+"</td>"
          "<td>"+String(mbTCP.Ireg(ib+REG_OUTPUT_VOLTAGE))+"</td>"
          "<td>"+String(mbTCP.Ireg(ib+REG_OUTPUT_POWER))+"</td>"
          "<td>"+String(mbTCP.Ireg(ib+REG_DC_BUS_VOLTAGE))+"</td>"
          "<td>"+String(mbTCP.Ireg(ib+REG_RPM))+"</td></tr>";
  }
  html+="</table></div><p><a href='/' style='display:inline-block;padding:8px 12px;background:#1976d2;color:#fff;border-radius:6px;text-decoration:none'>Back</a></p></body></html>";
  server.send(200,"text/html", html);
}

// ====== MONITOR PAMIĘCI – Zasoby (/resources) [DODANE] ======
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
              "async function refresh(){"
              " try{const d=await fetch('/resources/data',{cache:'no-store'}).then(r=>r.json());fill(d);}catch(e){console.error(e);} "
              " setTimeout(refresh,10000);}"
              "function fill(d){"
              " document.getElementById('free_heap').textContent=d.free_heap+' B';"
              " document.getElementById('min_free_heap').textContent=d.min_free_heap+' B';"
              " document.getElementById('largest_free_block').textContent=d.largest_free_block+' B';"
              " document.getElementById('uptime').textContent=(d.uptime_ms/1000).toFixed(0)+' s';"
              "}"
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
              "<p><a href='/' style='display:inline-block;padding:8px 12px;background:#1976d2;color:#fff;text-decoration:none;border-radius:6px'>Back</a></p>"
              "</div></body></html>";
  server.send(200,"text/html",html);
}
static void handleResourcesData(){
  if(!requireAuth()) return;
  lastMemSample=millis();
  server.send(200,"application/json", buildMemJson());
}

// ===== Routing =====
static void setupWeb(){
  server.on("/", handleRoot);

  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);

  server.on("/inverter", HTTP_GET, handleInverterPage);
  server.on("/inverter/state", HTTP_GET, handleInverterState);
  server.on("/inverter/state_all", HTTP_GET, handleInverterStateAll);
  server.on("/inverter/cmd", HTTP_GET, handleInverterCmd);

  server.on("/io", HTTP_GET, handleIOPage);
  server.on("/io/state", HTTP_GET, handleIOState);
  server.on("/io/set", HTTP_GET, handleIOSet);
  server.on("/io/diag", HTTP_GET, handleIODiag);

  server.on("/critical", HTTP_GET, handleCritical);

  server.on("/mqtt/repub", HTTP_GET, handleMqttTopics);
  server.on("/mqtt/repub/ui", HTTP_GET, handleMqttRepubUI);
  server.on("/mqtt/repub/publish", HTTP_GET, handleMqttRepubPublish);
  server.on("/mqtt/repub/set", HTTP_POST, handleMqttRepubSetPublish);

  server.on("/modbus/tcp", HTTP_GET, handleModbusTCPPage);

  // DODANE: Zasoby (monitor pamięci)
  server.on("/resources", HTTP_GET, handleResourcesPage);
  server.on("/resources/data", HTTP_GET, handleResourcesData);

  server.begin();
  Serial.println("[HTTP] Server started");
}

// ===== Tasks =====
static void taskNet(void*){
  for(;;){
    server.handleClient();
    mbTCP.task(); // <<< POPRAWKA: obsługa ModbusTCP (dodane)
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
    if(rtuCfg.pollMs && (millis()%rtuCfg.pollMs)<20){
      uint8_t sid=sid_list[current_sid_index];
      if(!rtu_freq_initialized[sid]){
        uint16_t rep=mbTCP.Ireg(IREG_BASE(sid)+REG_OUTPUT_FREQ);
        rtuSyncInitialFrequency(sid, rep, rep);
      }
      current_sid_index=(current_sid_index+1)%sid_count;
    }
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
  Serial.println("[BOOT] KC868-A16 Multi-SID + Full WWW");
  loadCfg();
  setupETH();
  setupWiFiInitial();
  setupIO();
  setupMQTT();
  setupMBTCP();
  setupWeb();
  inverter_master_begin();

  // DODANE: jednorazowy log pamięci po inicjalizacji
  Serial.printf("[MEM] Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("[MEM] Min free heap: %u bytes\n", ESP.getMinFreeHeap());
  Serial.printf("[MEM] Largest free block: %u bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

  xTaskCreatePinnedToCore(taskNet,"NET",8192,nullptr,1,nullptr,0);
  xTaskCreatePinnedToCore(taskIO,"IO", 6144,nullptr,1,nullptr,1);
  Serial.println("[READY]");
}
void loop(){ vTaskDelay(100/portTICK_PERIOD_MS); }