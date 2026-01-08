// Test configuration - DO NOT use in production
#ifndef CONFIG_TEST_H
#define CONFIG_TEST_H

// Test WiFi Configuration
#define WIFI_SSID "TestNetwork"
#define WIFI_PASSWORD "TestPassword123"

// Test MQTT Configuration
#define MQTT_SERVER "test.mosquitto.org"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""
#define MQTT_CLIENT_ID "VPC_M0701S_Test"

// MQTT Topics
#define MQTT_BASE_TOPIC "test/inverter/vpc_m0701s"
#define MQTT_STATUS_TOPIC MQTT_BASE_TOPIC "/status"
#define MQTT_COMMAND_TOPIC MQTT_BASE_TOPIC "/command"
#define MQTT_FREQUENCY_TOPIC MQTT_BASE_TOPIC "/frequency"
#define MQTT_CURRENT_TOPIC MQTT_BASE_TOPIC "/current"
#define MQTT_VOLTAGE_TOPIC MQTT_BASE_TOPIC "/voltage"
#define MQTT_POWER_TOPIC MQTT_BASE_TOPIC "/power"
#define MQTT_TEMP_TOPIC MQTT_BASE_TOPIC "/temperature"
#define MQTT_RPM_TOPIC MQTT_BASE_TOPIC "/rpm"
#define MQTT_FAULT_TOPIC MQTT_BASE_TOPIC "/fault"
#define MQTT_DIAGNOSTIC_TOPIC MQTT_BASE_TOPIC "/diagnostic"

// Modbus Configuration
#define MODBUS_BAUD_RATE 9600
#define MODBUS_RX_PIN 16
#define MODBUS_TX_PIN 17
#define MODBUS_DE_PIN 4

// VPC M0701S Default Slave ID
#define VPC_M0701S_SLAVE_ID 1

// Multi-SID support
#define MAX_INVERTERS 4
#define POLL_INTERVAL 1000

// Web Server Configuration
#define WEB_SERVER_PORT 80

// Update intervals
#define STATUS_UPDATE_INTERVAL 2000
#define MQTT_RECONNECT_INTERVAL 5000

#endif // CONFIG_TEST_H
