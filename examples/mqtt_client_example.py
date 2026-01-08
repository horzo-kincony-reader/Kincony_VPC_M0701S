"""
VPC M0701S MQTT Client Example
Python script to interact with the VPC M0701S controller via MQTT

Requirements:
    pip install paho-mqtt

Usage:
    python mqtt_client_example.py
"""

import paho.mqtt.client as mqtt
import json
import time
import sys

# Configuration
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_USER = "mqtt_user"
MQTT_PASSWORD = "mqtt_password"
BASE_TOPIC = "inverter/vpc_m0701s"
INVERTER_ID = 0  # Default to first inverter

# MQTT Callbacks
def on_connect(client, userdata, flags, rc):
    """Callback when connected to MQTT broker"""
    if rc == 0:
        print(f"✓ Connected to MQTT broker at {MQTT_BROKER}:{MQTT_PORT}")
        
        # Subscribe to status topics
        status_topic = f"{BASE_TOPIC}/{INVERTER_ID}/status"
        client.subscribe(status_topic)
        print(f"✓ Subscribed to {status_topic}")
        
        # Subscribe to individual parameter topics
        topics = ["frequency", "current", "voltage", "power", "temperature", "rpm", "fault"]
        for topic in topics:
            full_topic = f"{BASE_TOPIC}/{INVERTER_ID}/{topic}"
            client.subscribe(full_topic)
        
    else:
        print(f"✗ Connection failed with code {rc}")

def on_message(client, userdata, msg):
    """Callback when message received"""
    topic = msg.topic
    payload = msg.payload.decode()
    
    # Parse JSON status messages
    if topic.endswith("/status"):
        try:
            data = json.loads(payload)
            print("\n" + "="*50)
            print(f"Inverter {data.get('inverter', 'N/A')} Status Update")
            print("="*50)
            print(f"Model:       {data.get('model', 'N/A')}")
            print(f"Connected:   {data.get('connected', False)}")
            print(f"Status:      {data.get('statusText', 'Unknown')}")
            print(f"Frequency:   {data.get('frequency', 0):.2f} Hz")
            print(f"Current:     {data.get('current', 0):.1f} A")
            print(f"Voltage:     {data.get('voltage', 0)} V")
            print(f"Power:       {data.get('power', 0):.2f} kW")
            print(f"Temperature: {data.get('temperature', 0):.1f} °C")
            print(f"RPM:         {data.get('rpm', 0)}")
            if data.get('faultCode', 0) != 0:
                print(f"FAULT:       {data.get('faultText', 'Unknown')}")
            print("="*50)
        except json.JSONDecodeError:
            print(f"Error parsing JSON from {topic}: {payload}")
    else:
        # Individual parameter update
        param = topic.split('/')[-1]
        print(f"[{param.upper()}] {payload}")

def on_disconnect(client, userdata, rc):
    """Callback when disconnected"""
    if rc != 0:
        print(f"⚠ Unexpected disconnection (code {rc})")
    else:
        print("✓ Disconnected from MQTT broker")

# Command functions
def send_command(client, inverter_id, command):
    """Send a command to the inverter"""
    topic = f"{BASE_TOPIC}/{inverter_id}/command"
    client.publish(topic, command)
    print(f"→ Sent command '{command}' to inverter {inverter_id}")

def set_frequency(client, inverter_id, frequency):
    """Set the frequency setpoint"""
    if 0 <= frequency <= 400:
        topic = f"{BASE_TOPIC}/{inverter_id}/frequency/set"
        client.publish(topic, str(frequency))
        print(f"→ Set frequency to {frequency} Hz for inverter {inverter_id}")
    else:
        print(f"✗ Frequency {frequency} out of range (0-400 Hz)")

def interactive_menu(client):
    """Display interactive menu for user commands"""
    print("\n" + "="*50)
    print("VPC M0701S MQTT Control Menu")
    print("="*50)
    print("1. Start motor")
    print("2. Stop motor")
    print("3. Set frequency")
    print("4. Reset fault")
    print("5. Emergency stop")
    print("6. Monitor only (no commands)")
    print("0. Exit")
    print("="*50)
    
    while True:
        try:
            choice = input("\nEnter choice (0-6): ").strip()
            
            if choice == "0":
                print("Exiting...")
                return False
            elif choice == "1":
                send_command(client, INVERTER_ID, "START")
            elif choice == "2":
                send_command(client, INVERTER_ID, "STOP")
            elif choice == "3":
                freq = float(input("Enter frequency (0-400 Hz): "))
                set_frequency(client, INVERTER_ID, freq)
            elif choice == "4":
                send_command(client, INVERTER_ID, "RESET")
            elif choice == "5":
                confirm = input("Emergency stop? (yes/no): ")
                if confirm.lower() == "yes":
                    send_command(client, INVERTER_ID, "EMERGENCY_STOP")
            elif choice == "6":
                print("Monitoring mode - waiting for status updates...")
                time.sleep(10)
            else:
                print("Invalid choice")
                
        except KeyboardInterrupt:
            print("\n\nInterrupted by user")
            return False
        except ValueError:
            print("Invalid input")

def main():
    """Main function"""
    print("VPC M0701S MQTT Client")
    print("="*50)
    
    # Create MQTT client
    client = mqtt.Client(client_id="vpc_m0701s_python_client")
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    
    # Set callbacks
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    
    # Connect to broker
    try:
        print(f"Connecting to {MQTT_BROKER}:{MQTT_PORT}...")
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        
        # Start network loop in background
        client.loop_start()
        
        # Wait for connection
        time.sleep(2)
        
        # Run interactive menu
        interactive_menu(client)
        
        # Clean disconnect
        client.loop_stop()
        client.disconnect()
        
    except Exception as e:
        print(f"✗ Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
