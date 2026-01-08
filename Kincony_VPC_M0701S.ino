/*
 * Kincony VPC M0701S Inverter Controller
 * 
 * This sketch provides complete control and monitoring of VPC M0701S inverters
 * via Modbus RTU with MQTT integration and web-based control panel.
 * 
 * Features:
 * - Modbus RTU communication with VPC M0701S inverters
 * - Multi-inverter support (up to 4 units with different slave IDs)
 * - MQTT interface for remote control and monitoring
 * - Web-based control panel for local access
 * - Real-time status monitoring and diagnostics
 * - Support for Start, Stop, Frequency Control commands
 * 
 * Hardware Requirements:
 * - ESP32 development board
 * - RS485 to TTL converter module
 * - VPC M0701S inverter(s)
 * 
 * Author: Kincony VPC M0701S Team
 * Version: 1.0.0
 */

#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <ArduinoJson.h>
#include "config.h"
#include "modbus_registers.h"

// Global objects
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer webServer(WEB_SERVER_PORT);
ModbusMaster modbus;

// Inverter data structure
struct InverterData {
  uint8_t slaveId;
  bool enabled;
  bool connected;
  uint16_t status;
  float frequency;
  float current;
  float voltage;
  float power;
  float temperature;
  uint16_t rpm;
  uint16_t faultCode;
  unsigned long lastUpdate;
  String model;
};

// Array to support multiple inverters
InverterData inverters[MAX_INVERTERS];
int activeInverters = 0;

// Timing variables
unsigned long lastStatusUpdate = 0;
unsigned long lastMqttReconnect = 0;
unsigned long lastModbusPoll = 0;
int currentInverterIndex = 0;

// Forward declarations
void setupWiFi();
void setupMQTT();
void setupWebServer();
void setupModbus();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleModbusPoll();
void handleWebRoot();
void handleWebAPI();
void handleWebControl();
void handleWebStatus();
void handleWebDiagnostics();
void publishStatus(int index);
void processCommand(int inverterIndex, String command, float value = 0);
String getFaultDescription(uint16_t faultCode);
String getStatusDescription(uint16_t status);
void preTransmission();
void postTransmission();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("Kincony VPC M0701S Inverter Controller");
  Serial.println("Version 1.0.0");
  Serial.println("========================================\n");
  
  // Initialize inverter array
  for (int i = 0; i < MAX_INVERTERS; i++) {
    inverters[i].slaveId = i + 1;
    inverters[i].enabled = (i == 0); // Only first inverter enabled by default
    inverters[i].connected = false;
    inverters[i].status = 0;
    inverters[i].frequency = 0;
    inverters[i].current = 0;
    inverters[i].voltage = 0;
    inverters[i].power = 0;
    inverters[i].temperature = 0;
    inverters[i].rpm = 0;
    inverters[i].faultCode = 0;
    inverters[i].lastUpdate = 0;
    inverters[i].model = "VPC-M0701S";
  }
  activeInverters = 1;
  
  // Setup communications
  setupWiFi();
  setupMQTT();
  setupModbus();
  setupWebServer();
  
  Serial.println("\n[INFO] System initialization complete");
  Serial.println("[INFO] Ready to communicate with inverters");
}

void loop() {
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WARN] WiFi disconnected, reconnecting...");
    setupWiFi();
  }
  
  // Maintain MQTT connection
  if (!mqttClient.connected()) {
    if (millis() - lastMqttReconnect > MQTT_RECONNECT_INTERVAL) {
      reconnectMQTT();
      lastMqttReconnect = millis();
    }
  } else {
    mqttClient.loop();
  }
  
  // Handle web server requests
  webServer.handleClient();
  
  // Poll Modbus devices
  if (millis() - lastModbusPoll > POLL_INTERVAL) {
    handleModbusPoll();
    lastModbusPoll = millis();
  }
  
  // Publish status updates
  if (millis() - lastStatusUpdate > STATUS_UPDATE_INTERVAL) {
    for (int i = 0; i < activeInverters; i++) {
      if (inverters[i].enabled && inverters[i].connected) {
        publishStatus(i);
      }
    }
    lastStatusUpdate = millis();
  }
}

void setupWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.print(WIFI_SSID);
  Serial.print("...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Connection failed!");
  }
}

void setupMQTT() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  Serial.println("[MQTT] MQTT client configured");
  reconnectMQTT();
}

void reconnectMQTT() {
  if (!mqttClient.connected()) {
    Serial.print("[MQTT] Connecting to MQTT broker...");
    
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println(" Connected!");
      
      // Subscribe to command topics for all active inverters
      for (int i = 0; i < activeInverters; i++) {
        if (inverters[i].enabled) {
          String cmdTopic = String(MQTT_BASE_TOPIC) + "/" + String(i) + "/command";
          String freqTopic = String(MQTT_BASE_TOPIC) + "/" + String(i) + "/frequency/set";
          
          mqttClient.subscribe(cmdTopic.c_str());
          mqttClient.subscribe(freqTopic.c_str());
          
          Serial.print("[MQTT] Subscribed to: ");
          Serial.println(cmdTopic);
        }
      }
      
      // Also subscribe to global command topic
      mqttClient.subscribe(MQTT_COMMAND_TOPIC);
      
    } else {
      Serial.print(" Failed, rc=");
      Serial.println(mqttClient.state());
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("[MQTT] Message received on ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);
  
  // Parse topic to determine inverter index
  String topicStr = String(topic);
  int inverterIndex = 0;
  
  // Check if topic contains inverter index
  if (topicStr.startsWith(String(MQTT_BASE_TOPIC) + "/")) {
    int firstSlash = topicStr.indexOf('/', String(MQTT_BASE_TOPIC).length() + 1);
    if (firstSlash > 0) {
      String indexStr = topicStr.substring(String(MQTT_BASE_TOPIC).length() + 1, firstSlash);
      inverterIndex = indexStr.toInt();
    }
  }
  
  // Process command
  if (topicStr.endsWith("/command")) {
    message.toUpperCase();
    processCommand(inverterIndex, message);
  } else if (topicStr.endsWith("/frequency/set")) {
    float freq = message.toFloat();
    processCommand(inverterIndex, "FREQ", freq);
  }
}

void setupModbus() {
  // Configure RS485 pins
  pinMode(MODBUS_DE_PIN, OUTPUT);
  digitalWrite(MODBUS_DE_PIN, LOW);
  
  // Initialize Modbus serial communication
  Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
  
  // Initialize Modbus Master
  modbus.begin(VPC_M0701S_SLAVE_ID, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);
  
  Serial.println("[Modbus] Modbus RTU initialized");
  Serial.print("[Modbus] Baud rate: ");
  Serial.println(MODBUS_BAUD_RATE);
  Serial.print("[Modbus] RX: ");
  Serial.print(MODBUS_RX_PIN);
  Serial.print(", TX: ");
  Serial.print(MODBUS_TX_PIN);
  Serial.print(", DE: ");
  Serial.println(MODBUS_DE_PIN);
}

void preTransmission() {
  digitalWrite(MODBUS_DE_PIN, HIGH);
  delayMicroseconds(100);
}

void postTransmission() {
  delayMicroseconds(100);
  digitalWrite(MODBUS_DE_PIN, LOW);
}

void handleModbusPoll() {
  if (activeInverters == 0) return;
  
  // Round-robin polling of active inverters
  int idx = currentInverterIndex;
  if (!inverters[idx].enabled) {
    currentInverterIndex = (currentInverterIndex + 1) % activeInverters;
    return;
  }
  
  // Set slave ID for current inverter
  modbus.begin(inverters[idx].slaveId, Serial2);
  
  uint8_t result;
  uint16_t data[16];
  
  // Read status and frequency
  result = modbus.readInputRegisters(REG_RUNNING_STATUS, 8);
  
  if (result == modbus.ku8MBSuccess) {
    inverters[idx].connected = true;
    inverters[idx].status = modbus.getResponseBuffer(0);
    inverters[idx].frequency = REG_TO_FREQ(modbus.getResponseBuffer(1));
    inverters[idx].current = REG_TO_CURRENT(modbus.getResponseBuffer(2));
    inverters[idx].voltage = modbus.getResponseBuffer(3);
    inverters[idx].power = REG_TO_POWER(modbus.getResponseBuffer(5));
    inverters[idx].rpm = modbus.getResponseBuffer(7);
    inverters[idx].lastUpdate = millis();
    
    // Read temperature and fault code separately
    result = modbus.readInputRegisters(REG_INVERTER_TEMP, 2);
    if (result == modbus.ku8MBSuccess) {
      inverters[idx].temperature = REG_TO_TEMP(modbus.getResponseBuffer(0));
      inverters[idx].faultCode = modbus.getResponseBuffer(1);
    }
    
  } else {
    if (inverters[idx].connected) {
      Serial.print("[Modbus] Communication error with inverter ");
      Serial.print(idx);
      Serial.print(" (Slave ID ");
      Serial.print(inverters[idx].slaveId);
      Serial.println(")");
    }
    inverters[idx].connected = false;
  }
  
  // Move to next inverter
  currentInverterIndex = (currentInverterIndex + 1) % activeInverters;
}

void processCommand(int inverterIndex, String command, float value) {
  if (inverterIndex >= activeInverters || !inverters[inverterIndex].enabled) {
    Serial.println("[ERROR] Invalid inverter index");
    return;
  }
  
  Serial.print("[Command] Processing '");
  Serial.print(command);
  Serial.print("' for inverter ");
  Serial.println(inverterIndex);
  
  modbus.begin(inverters[inverterIndex].slaveId, Serial2);
  uint8_t result;
  
  if (command == "START" || command == "START_FORWARD") {
    result = modbus.writeSingleRegister(REG_CONTROL_COMMAND, CMD_START_FORWARD);
    if (result == modbus.ku8MBSuccess) {
      Serial.println("[Command] Start command sent successfully");
      inverters[inverterIndex].status |= STATUS_RUNNING;
    }
  } 
  else if (command == "STOP") {
    result = modbus.writeSingleRegister(REG_CONTROL_COMMAND, CMD_STOP);
    if (result == modbus.ku8MBSuccess) {
      Serial.println("[Command] Stop command sent successfully");
      inverters[inverterIndex].status &= ~STATUS_RUNNING;
    }
  }
  else if (command == "START_REVERSE") {
    result = modbus.writeSingleRegister(REG_CONTROL_COMMAND, CMD_START_REVERSE);
    if (result == modbus.ku8MBSuccess) {
      Serial.println("[Command] Reverse start command sent successfully");
    }
  }
  else if (command == "RESET" || command == "RESET_FAULT") {
    result = modbus.writeSingleRegister(REG_CONTROL_COMMAND, CMD_RESET_FAULT);
    if (result == modbus.ku8MBSuccess) {
      Serial.println("[Command] Fault reset command sent successfully");
      inverters[inverterIndex].faultCode = 0;
    }
  }
  else if (command == "FREQ" || command == "FREQUENCY") {
    if (value >= 0 && value <= 400) { // Typical VFD frequency range
      uint16_t freqReg = FREQ_TO_REG(value);
      result = modbus.writeSingleRegister(REG_FREQUENCY_SETPOINT, freqReg);
      if (result == modbus.ku8MBSuccess) {
        Serial.print("[Command] Frequency set to ");
        Serial.print(value);
        Serial.println(" Hz");
        inverters[inverterIndex].frequency = value;
      }
    } else {
      Serial.println("[ERROR] Frequency out of range (0-400 Hz)");
    }
  }
  else if (command == "EMERGENCY_STOP") {
    result = modbus.writeSingleRegister(REG_CONTROL_COMMAND, CMD_EMERGENCY_STOP);
    if (result == modbus.ku8MBSuccess) {
      Serial.println("[Command] Emergency stop activated");
    }
  }
  else {
    Serial.print("[ERROR] Unknown command: ");
    Serial.println(command);
  }
}

void publishStatus(int index) {
  if (!mqttClient.connected()) return;
  
  String baseTopic = String(MQTT_BASE_TOPIC) + "/" + String(index);
  
  // Create JSON status document
  StaticJsonDocument<512> doc;
  doc["inverter"] = index;
  doc["slaveId"] = inverters[index].slaveId;
  doc["model"] = inverters[index].model;
  doc["connected"] = inverters[index].connected;
  doc["status"] = inverters[index].status;
  doc["statusText"] = getStatusDescription(inverters[index].status);
  doc["frequency"] = inverters[index].frequency;
  doc["current"] = inverters[index].current;
  doc["voltage"] = inverters[index].voltage;
  doc["power"] = inverters[index].power;
  doc["temperature"] = inverters[index].temperature;
  doc["rpm"] = inverters[index].rpm;
  doc["faultCode"] = inverters[index].faultCode;
  doc["faultText"] = getFaultDescription(inverters[index].faultCode);
  doc["lastUpdate"] = inverters[index].lastUpdate;
  
  String jsonStr;
  serializeJson(doc, jsonStr);
  
  // Publish to status topic
  mqttClient.publish((baseTopic + "/status").c_str(), jsonStr.c_str(), true);
  
  // Publish individual values
  mqttClient.publish((baseTopic + "/frequency").c_str(), String(inverters[index].frequency).c_str());
  mqttClient.publish((baseTopic + "/current").c_str(), String(inverters[index].current).c_str());
  mqttClient.publish((baseTopic + "/voltage").c_str(), String(inverters[index].voltage).c_str());
  mqttClient.publish((baseTopic + "/power").c_str(), String(inverters[index].power).c_str());
  mqttClient.publish((baseTopic + "/temperature").c_str(), String(inverters[index].temperature).c_str());
  mqttClient.publish((baseTopic + "/rpm").c_str(), String(inverters[index].rpm).c_str());
  
  if (inverters[index].faultCode != 0) {
    mqttClient.publish((baseTopic + "/fault").c_str(), 
                      (String(inverters[index].faultCode) + ":" + 
                       getFaultDescription(inverters[index].faultCode)).c_str());
  }
}

String getStatusDescription(uint16_t status) {
  String desc = "";
  if (status & STATUS_READY) desc += "Ready ";
  if (status & STATUS_RUNNING) desc += "Running ";
  if (status & STATUS_FORWARD) desc += "Forward ";
  if (status & STATUS_REVERSE) desc += "Reverse ";
  if (status & STATUS_FAULT) desc += "FAULT ";
  if (status & STATUS_WARNING) desc += "Warning ";
  if (status & STATUS_AT_FREQUENCY) desc += "AtFreq ";
  if (status & STATUS_OVERLOAD) desc += "Overload ";
  if (desc.length() == 0) desc = "Stopped";
  return desc;
}

String getFaultDescription(uint16_t faultCode) {
  switch (faultCode) {
    case FAULT_NONE: return "No Fault";
    case FAULT_OVERCURRENT: return "Overcurrent";
    case FAULT_OVERVOLTAGE: return "Overvoltage";
    case FAULT_UNDERVOLTAGE: return "Undervoltage";
    case FAULT_OVERLOAD: return "Overload";
    case FAULT_OVERTEMP: return "Overtemperature";
    case FAULT_MOTOR_OVERLOAD: return "Motor Overload";
    case FAULT_EXTERNAL: return "External Fault";
    case FAULT_COMM_ERROR: return "Communication Error";
    case FAULT_PHASE_LOSS: return "Phase Loss";
    default: return "Unknown Fault (" + String(faultCode) + ")";
  }
}

void setupWebServer() {
  webServer.on("/", handleWebRoot);
  webServer.on("/api", HTTP_GET, handleWebAPI);
  webServer.on("/control", HTTP_POST, handleWebControl);
  webServer.on("/status", HTTP_GET, handleWebStatus);
  webServer.on("/diagnostics", HTTP_GET, handleWebDiagnostics);
  
  webServer.begin();
  Serial.println("[Web] Web server started on port 80");
  Serial.print("[Web] Access at http://");
  Serial.println(WiFi.localIP());
}

void handleWebRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <title>VPC M0701S Control Panel</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: Arial, sans-serif; 
      margin: 20px; 
      background-color: #f0f0f0;
    }
    .container { 
      max-width: 1200px; 
      margin: 0 auto; 
      background: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
    }
    h1 { 
      color: #333; 
      border-bottom: 3px solid #4CAF50;
      padding-bottom: 10px;
    }
    h2 { 
      color: #666; 
      margin-top: 30px;
    }
    .inverter-card {
      border: 1px solid #ddd;
      padding: 15px;
      margin: 10px 0;
      border-radius: 5px;
      background: #fafafa;
    }
    .status-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 10px;
      margin: 15px 0;
    }
    .status-item {
      background: white;
      padding: 10px;
      border-radius: 5px;
      border-left: 4px solid #4CAF50;
    }
    .status-label {
      font-size: 12px;
      color: #666;
      text-transform: uppercase;
    }
    .status-value {
      font-size: 24px;
      font-weight: bold;
      color: #333;
    }
    .status-unit {
      font-size: 14px;
      color: #999;
    }
    .controls {
      margin: 20px 0;
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
    }
    button {
      padding: 12px 24px;
      font-size: 16px;
      cursor: pointer;
      border: none;
      border-radius: 5px;
      transition: all 0.3s;
    }
    .btn-start {
      background-color: #4CAF50;
      color: white;
    }
    .btn-start:hover {
      background-color: #45a049;
    }
    .btn-stop {
      background-color: #f44336;
      color: white;
    }
    .btn-stop:hover {
      background-color: #da190b;
    }
    .btn-reset {
      background-color: #ff9800;
      color: white;
    }
    .btn-reset:hover {
      background-color: #e68900;
    }
    .freq-control {
      display: flex;
      align-items: center;
      gap: 10px;
      margin: 20px 0;
    }
    input[type="number"] {
      padding: 10px;
      font-size: 16px;
      border: 2px solid #ddd;
      border-radius: 5px;
      width: 100px;
    }
    .connected {
      color: #4CAF50;
      font-weight: bold;
    }
    .disconnected {
      color: #f44336;
      font-weight: bold;
    }
    .fault {
      background-color: #ffebee;
      border-left-color: #f44336 !important;
    }
    .running {
      border-left-color: #4CAF50 !important;
    }
    .stopped {
      border-left-color: #999 !important;
    }
    .refresh-info {
      font-size: 12px;
      color: #999;
      margin: 10px 0;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🔌 VPC M0701S Inverter Control Panel</h1>
    <div class="refresh-info">Auto-refresh every 2 seconds | Last update: <span id="lastUpdate">-</span></div>
    
    <div id="inverters"></div>
  </div>

  <script>
    let activeInverters = )";
  
  html += String(activeInverters);
  html += R"(;

    function updateStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          const container = document.getElementById('inverters');
          container.innerHTML = '';
          
          data.inverters.forEach((inv, idx) => {
            const card = document.createElement('div');
            card.className = 'inverter-card';
            
            let statusClass = inv.connected ? 'connected' : 'disconnected';
            let cardClass = inv.faultCode != 0 ? 'fault' : 
                           (inv.status & 0x0002) ? 'running' : 'stopped';
            
            card.innerHTML = `
              <h2>Inverter ${idx} - ${inv.model} (Slave ID: ${inv.slaveId})</h2>
              <p>Status: <span class="${statusClass}">${inv.connected ? 'Connected' : 'Disconnected'}</span></p>
              
              <div class="status-grid">
                <div class="status-item ${cardClass}">
                  <div class="status-label">State</div>
                  <div class="status-value">${inv.statusText}</div>
                </div>
                <div class="status-item">
                  <div class="status-label">Frequency</div>
                  <div class="status-value">${inv.frequency.toFixed(2)} <span class="status-unit">Hz</span></div>
                </div>
                <div class="status-item">
                  <div class="status-label">Current</div>
                  <div class="status-value">${inv.current.toFixed(1)} <span class="status-unit">A</span></div>
                </div>
                <div class="status-item">
                  <div class="status-label">Voltage</div>
                  <div class="status-value">${inv.voltage} <span class="status-unit">V</span></div>
                </div>
                <div class="status-item">
                  <div class="status-label">Power</div>
                  <div class="status-value">${inv.power.toFixed(2)} <span class="status-unit">kW</span></div>
                </div>
                <div class="status-item">
                  <div class="status-label">Temperature</div>
                  <div class="status-value">${inv.temperature.toFixed(1)} <span class="status-unit">°C</span></div>
                </div>
                <div class="status-item">
                  <div class="status-label">RPM</div>
                  <div class="status-value">${inv.rpm}</div>
                </div>
                <div class="status-item ${inv.faultCode != 0 ? 'fault' : ''}">
                  <div class="status-label">Fault</div>
                  <div class="status-value" style="font-size: 16px;">${inv.faultText}</div>
                </div>
              </div>
              
              <div class="controls">
                <button class="btn-start" onclick="sendCommand(${idx}, 'START')">▶ Start</button>
                <button class="btn-stop" onclick="sendCommand(${idx}, 'STOP')">⏹ Stop</button>
                <button class="btn-reset" onclick="sendCommand(${idx}, 'RESET')">🔄 Reset Fault</button>
              </div>
              
              <div class="freq-control">
                <label for="freq${idx}">Set Frequency:</label>
                <input type="number" id="freq${idx}" min="0" max="400" step="0.1" value="${inv.frequency.toFixed(1)}">
                <button onclick="setFrequency(${idx})">Set</button>
                <span style="color: #666; font-size: 12px;">(0-400 Hz)</span>
              </div>
            `;
            
            container.appendChild(card);
          });
          
          document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
        })
        .catch(error => {
          console.error('Error fetching status:', error);
        });
    }

    function sendCommand(inverter, command) {
      fetch('/control', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: `inverter=${inverter}&command=${command}`
      })
      .then(response => response.text())
      .then(data => {
        console.log('Command response:', data);
        setTimeout(updateStatus, 500);
      })
      .catch(error => {
        console.error('Error sending command:', error);
        alert('Failed to send command: ' + error);
      });
    }

    function setFrequency(inverter) {
      const freq = document.getElementById('freq' + inverter).value;
      fetch('/control', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: `inverter=${inverter}&command=FREQ&value=${freq}`
      })
      .then(response => response.text())
      .then(data => {
        console.log('Frequency set:', data);
        setTimeout(updateStatus, 500);
      })
      .catch(error => {
        console.error('Error setting frequency:', error);
        alert('Failed to set frequency: ' + error);
      });
    }

    // Initial update and auto-refresh
    updateStatus();
    setInterval(updateStatus, 2000);
  </script>
</body>
</html>
  )";
  
  webServer.send(200, "text/html", html);
}

void handleWebStatus() {
  StaticJsonDocument<2048> doc;
  JsonArray invArray = doc.createNestedArray("inverters");
  
  for (int i = 0; i < activeInverters; i++) {
    JsonObject inv = invArray.createNestedObject();
    inv["index"] = i;
    inv["slaveId"] = inverters[i].slaveId;
    inv["model"] = inverters[i].model;
    inv["enabled"] = inverters[i].enabled;
    inv["connected"] = inverters[i].connected;
    inv["status"] = inverters[i].status;
    inv["statusText"] = getStatusDescription(inverters[i].status);
    inv["frequency"] = inverters[i].frequency;
    inv["current"] = inverters[i].current;
    inv["voltage"] = inverters[i].voltage;
    inv["power"] = inverters[i].power;
    inv["temperature"] = inverters[i].temperature;
    inv["rpm"] = inverters[i].rpm;
    inv["faultCode"] = inverters[i].faultCode;
    inv["faultText"] = getFaultDescription(inverters[i].faultCode);
    inv["lastUpdate"] = inverters[i].lastUpdate;
  }
  
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

void handleWebControl() {
  if (!webServer.hasArg("inverter") || !webServer.hasArg("command")) {
    webServer.send(400, "text/plain", "Missing parameters");
    return;
  }
  
  int inverter = webServer.arg("inverter").toInt();
  String command = webServer.arg("command");
  float value = 0;
  
  if (webServer.hasArg("value")) {
    value = webServer.arg("value").toFloat();
  }
  
  processCommand(inverter, command, value);
  webServer.send(200, "text/plain", "OK");
}

void handleWebAPI() {
  StaticJsonDocument<512> doc;
  doc["version"] = "1.0.0";
  doc["device"] = "Kincony VPC M0701S Controller";
  doc["uptime"] = millis() / 1000;
  doc["activeInverters"] = activeInverters;
  doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  doc["mqttConnected"] = mqttClient.connected();
  doc["ip"] = WiFi.localIP().toString();
  
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

void handleWebDiagnostics() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <title>VPC M0701S Diagnostics</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: monospace; margin: 20px; background: #1e1e1e; color: #d4d4d4; }
    .container { max-width: 1000px; margin: 0 auto; }
    h1 { color: #4ec9b0; }
    .log-entry { padding: 5px; border-bottom: 1px solid #3e3e3e; }
    .info { color: #4fc1ff; }
    .warn { color: #dcdcaa; }
    .error { color: #f48771; }
    table { width: 100%; border-collapse: collapse; margin: 20px 0; }
    th, td { padding: 10px; text-align: left; border: 1px solid #3e3e3e; }
    th { background: #2d2d2d; color: #4ec9b0; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🔍 System Diagnostics</h1>
    <table>
      <tr><th>Parameter</th><th>Value</th></tr>
      <tr><td>Device</td><td>Kincony VPC M0701S Controller</td></tr>
      <tr><td>Version</td><td>1.0.0</td></tr>
      <tr><td>Uptime</td><td>)";
  
  html += String(millis() / 1000);
  html += R"( seconds</td></tr>
      <tr><td>WiFi Status</td><td>)";
  html += (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
  html += R"(</td></tr>
      <tr><td>IP Address</td><td>)";
  html += WiFi.localIP().toString();
  html += R"(</td></tr>
      <tr><td>MQTT Status</td><td>)";
  html += mqttClient.connected() ? "Connected" : "Disconnected";
  html += R"(</td></tr>
      <tr><td>Active Inverters</td><td>)";
  html += String(activeInverters);
  html += R"(</td></tr>
      <tr><td>Free Heap</td><td>)";
  html += String(ESP.getFreeHeap());
  html += R"( bytes</td></tr>
      <tr><td>Modbus Baud</td><td>)";
  html += String(MODBUS_BAUD_RATE);
  html += R"(</td></tr>
    </table>
    
    <h2>Inverter Status</h2>
    <table>
      <tr>
        <th>Index</th>
        <th>Slave ID</th>
        <th>Connected</th>
        <th>Status</th>
        <th>Frequency</th>
        <th>Fault</th>
      </tr>
  )";
  
  for (int i = 0; i < activeInverters; i++) {
    html += "<tr>";
    html += "<td>" + String(i) + "</td>";
    html += "<td>" + String(inverters[i].slaveId) + "</td>";
    html += "<td>" + String(inverters[i].connected ? "Yes" : "No") + "</td>";
    html += "<td>" + getStatusDescription(inverters[i].status) + "</td>";
    html += "<td>" + String(inverters[i].frequency) + " Hz</td>";
    html += "<td>" + getFaultDescription(inverters[i].faultCode) + "</td>";
    html += "</tr>";
  }
  
  html += R"(
    </table>
    
    <p><a href="/" style="color: #4ec9b0;">← Back to Control Panel</a></p>
  </div>
</body>
</html>
  )";
  
  webServer.send(200, "text/html", html);
}
