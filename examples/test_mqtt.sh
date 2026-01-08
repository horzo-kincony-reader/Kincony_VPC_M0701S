#!/bin/bash
# VPC M0701S Quick Test Script
# Tests basic MQTT communication with the inverter controller

MQTT_HOST="localhost"
INVERTER_ID="0"
BASE_TOPIC="inverter/vpc_m0701s"

echo "========================================"
echo "VPC M0701S MQTT Test Script"
echo "========================================"
echo ""

# Check if mosquitto_pub is available
if ! command -v mosquitto_pub &> /dev/null; then
    echo "Error: mosquitto_pub not found"
    echo "Install with: sudo apt-get install mosquitto-clients"
    exit 1
fi

echo "Testing MQTT communication with inverter $INVERTER_ID"
echo "MQTT Host: $MQTT_HOST"
echo ""

# Subscribe to status in background
echo "1. Starting status monitor..."
mosquitto_sub -h $MQTT_HOST -t "$BASE_TOPIC/$INVERTER_ID/status" -C 1 &
MONITOR_PID=$!
sleep 2

echo ""
echo "2. Testing START command..."
mosquitto_pub -h $MQTT_HOST -t "$BASE_TOPIC/$INVERTER_ID/command" -m "START"
sleep 1

echo ""
echo "3. Setting frequency to 50 Hz..."
mosquitto_pub -h $MQTT_HOST -t "$BASE_TOPIC/$INVERTER_ID/frequency/set" -m "50.0"
sleep 2

echo ""
echo "4. Testing STOP command..."
mosquitto_pub -h $MQTT_HOST -t "$BASE_TOPIC/$INVERTER_ID/command" -m "STOP"
sleep 1

echo ""
echo "5. Reading final status..."
mosquitto_sub -h $MQTT_HOST -t "$BASE_TOPIC/$INVERTER_ID/status" -C 1

# Clean up
kill $MONITOR_PID 2>/dev/null

echo ""
echo "========================================"
echo "Test completed"
echo "========================================"
