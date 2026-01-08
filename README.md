# VPC M0701S Inverter Controller

Complete Arduino/ESP32-based controller for the VPC M0701S Variable Frequency Drive (VFD) inverter with Modbus RTU communication, MQTT integration, and web-based control panel.

## Features

### Core Functionality
- ✅ **Modbus RTU Communication**: Full support for VPC M0701S Modbus register mappings
- ✅ **Multi-Inverter Support**: Control up to 4 inverters simultaneously with different slave IDs
- ✅ **MQTT Integration**: Remote monitoring and control via MQTT protocol
- ✅ **Web Control Panel**: Browser-based interface for local control and monitoring
- ✅ **Real-time Monitoring**: Continuous monitoring of frequency, current, voltage, power, temperature, and RPM
- ✅ **Diagnostic Support**: Built-in diagnostics and fault reporting

### Supported Commands
- Start/Stop motor operation
- Forward/Reverse rotation control
- Frequency setpoint adjustment (0-400 Hz)
- Fault reset
- Emergency stop
- JOG mode (forward/reverse)

### Monitored Parameters
- Running status and state
- Output frequency (Hz)
- Output current (A)
- Output voltage (V)
- DC bus voltage (V)
- Output power (kW)
- Output torque (%)
- Motor speed (RPM)
- Inverter temperature (°C)
- Fault codes with descriptions
- Digital input terminal status
- Total running time

## Hardware Requirements

### Required Components
1. **ESP32 Development Board** (ESP32-DevKitC or similar)
2. **RS485 to TTL Converter Module** (MAX485 or equivalent)
3. **VPC M0701S Inverter(s)**
4. **Power Supply**: 5V for ESP32, appropriate power for inverter
5. **Connecting Wires**

### Wiring Diagram

```
ESP32          RS485 Module       VPC M0701S
-----          -------------      ----------
GPIO17  -----> TX (DI)            
GPIO16  <----- RX (RO)            
GPIO4   -----> DE/RE              
GND     -----> GND                
5V      -----> VCC                

RS485 Module   VPC M0701S
-------------  ----------
A+      -----> A+
B-      -----> B-
GND     -----> GND (optional, for shielding)
```

### Pin Configuration
- **Modbus RX**: GPIO16
- **Modbus TX**: GPIO17
- **Driver Enable (DE)**: GPIO4
- **Baud Rate**: 9600 (configurable in config.h)

## Software Requirements

### Arduino Libraries
Install the following libraries via Arduino Library Manager:

```
- WiFi (built-in with ESP32)
- WebServer (built-in with ESP32)
- PubSubClient (by Nick O'Leary) - for MQTT
- ModbusMaster (by Doc Walker) - for Modbus RTU
- ArduinoJson (by Benoit Blanchon) - for JSON handling
```

### Installation Command
```bash
# Using Arduino CLI
arduino-cli lib install "PubSubClient" "ModbusMaster" "ArduinoJson"
```

## Configuration

### 1. WiFi Settings
Edit `config.h` to set your WiFi credentials:

```cpp
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
```

### 2. MQTT Settings
Configure your MQTT broker:

```cpp
#define MQTT_SERVER "192.168.1.100"
#define MQTT_PORT 1883
#define MQTT_USER "mqtt_user"
#define MQTT_PASSWORD "mqtt_password"
```

### 3. Modbus Settings
Adjust Modbus communication parameters if needed:

```cpp
#define MODBUS_BAUD_RATE 9600
#define VPC_M0701S_SLAVE_ID 1  // Default slave ID
#define MAX_INVERTERS 4        // Number of inverters to support
```

### 4. Multi-Inverter Setup
To enable multiple inverters:

1. Set each inverter to a unique Modbus slave ID (1, 2, 3, 4)
2. Update `activeInverters` in the code
3. Wire all inverters to the same RS485 bus (parallel connection)

## Usage

### Upload and Run

1. Open `Kincony_VPC_M0701S.ino` in Arduino IDE
2. Select your ESP32 board: Tools → Board → ESP32 Arduino → ESP32 Dev Module
3. Select the correct port: Tools → Port
4. Click Upload
5. Open Serial Monitor (115200 baud) to see system output

### Web Interface

After successful connection:

1. Find the ESP32 IP address in Serial Monitor
2. Open browser and navigate to: `http://<ESP32_IP>/`
3. Use the control panel to:
   - Start/Stop motors
   - Set frequency
   - Monitor real-time parameters
   - View fault status
   - Reset faults

### Web Endpoints

- `/` - Main control panel
- `/status` - JSON status of all inverters
- `/api` - System information
- `/diagnostics` - Detailed diagnostic page
- `/control` - POST endpoint for commands

### MQTT Topics

#### Subscribe (Commands)
```
inverter/vpc_m0701s/0/command       # Commands for inverter 0
inverter/vpc_m0701s/0/frequency/set # Set frequency for inverter 0
inverter/vpc_m0701s/1/command       # Commands for inverter 1
```

#### Publish (Status)
```
inverter/vpc_m0701s/0/status        # Full JSON status
inverter/vpc_m0701s/0/frequency     # Current frequency
inverter/vpc_m0701s/0/current       # Current draw
inverter/vpc_m0701s/0/voltage       # Output voltage
inverter/vpc_m0701s/0/power         # Output power
inverter/vpc_m0701s/0/temperature   # Inverter temperature
inverter/vpc_m0701s/0/rpm           # Motor speed
inverter/vpc_m0701s/0/fault         # Fault information
```

### MQTT Command Examples

Using mosquitto_pub:

```bash
# Start motor
mosquitto_pub -h localhost -t "inverter/vpc_m0701s/0/command" -m "START"

# Stop motor
mosquitto_pub -h localhost -t "inverter/vpc_m0701s/0/command" -m "STOP"

# Set frequency to 50 Hz
mosquitto_pub -h localhost -t "inverter/vpc_m0701s/0/frequency/set" -m "50.0"

# Reset fault
mosquitto_pub -h localhost -t "inverter/vpc_m0701s/0/command" -m "RESET"
```

## Testing

### Unit Tests

Run the included unit tests to verify functionality:

1. Open `test_vpc_m0701s.ino` in Arduino IDE
2. Upload to ESP32
3. Open Serial Monitor
4. Tests will run automatically and display results

Test coverage includes:
- Register conversion functions
- Command codes
- Status bits
- Fault codes
- Multi-inverter support
- Edge cases

### Integration Testing

1. Connect to a VPC M0701S inverter
2. Monitor Serial output for Modbus communication
3. Use web interface to send commands
4. Verify inverter responds correctly
5. Check MQTT topics for status updates

## Troubleshooting

### Communication Issues

**Problem**: "Communication error with inverter"
- Check RS485 wiring (A+, B-, GND)
- Verify baud rate matches inverter setting
- Confirm slave ID is correct
- Check RS485 module power supply
- Measure voltage on RS485 lines during transmission

**Problem**: "Modbus timeout"
- Increase `POLL_INTERVAL` in config.h
- Check for loose connections
- Verify inverter is powered on
- Test with single inverter first

### WiFi Issues

**Problem**: WiFi won't connect
- Verify SSID and password
- Check 2.4GHz WiFi availability (ESP32 doesn't support 5GHz)
- Move ESP32 closer to router
- Check Serial Monitor for connection attempts

### MQTT Issues

**Problem**: MQTT connection fails
- Verify broker address and port
- Check username/password if required
- Test broker with mosquitto_sub
- Ensure firewall allows connection

### Web Interface Issues

**Problem**: Can't access web page
- Verify ESP32 IP address
- Check WiFi connection
- Try accessing from same network
- Clear browser cache

## Modbus Register Map

### Holding Registers (Read/Write)

| Address | Name | Description | Resolution |
|---------|------|-------------|------------|
| 0x2000 | Control Command | Start/Stop/Reset | - |
| 0x2001 | Frequency Setpoint | Target frequency | 0.01 Hz |
| 0x2002 | Acceleration Time | Ramp-up time | 0.1 s |
| 0x2003 | Deceleration Time | Ramp-down time | 0.1 s |
| 0x2004 | Upper Frequency Limit | Maximum frequency | 0.01 Hz |
| 0x2005 | Lower Frequency Limit | Minimum frequency | 0.01 Hz |

### Input Registers (Read Only)

| Address | Name | Description | Resolution |
|---------|------|-------------|------------|
| 0x3000 | Running Status | Status word | - |
| 0x3001 | Output Frequency | Current frequency | 0.01 Hz |
| 0x3002 | Output Current | Current draw | 0.1 A |
| 0x3003 | Output Voltage | Output voltage | 1 V |
| 0x3004 | Bus Voltage | DC bus voltage | 1 V |
| 0x3005 | Output Power | Power output | 0.1 kW |
| 0x3006 | Output Torque | Torque percentage | 0.1 % |
| 0x3007 | Motor Speed | RPM | 1 RPM |
| 0x3008 | Inverter Temperature | Temperature | 0.1 °C |
| 0x3009 | Fault Code | Current fault | - |

## Status Codes

| Bit | Name | Description |
|-----|------|-------------|
| 0 | READY | Inverter ready |
| 1 | RUNNING | Motor running |
| 2 | FORWARD | Forward rotation |
| 3 | REVERSE | Reverse rotation |
| 4 | FAULT | Fault condition |
| 5 | WARNING | Warning active |
| 6 | AT_FREQUENCY | At target frequency |
| 7 | OVERLOAD | Overload condition |

## Fault Codes

| Code | Description |
|------|-------------|
| 0x00 | No Fault |
| 0x01 | Overcurrent |
| 0x02 | Overvoltage |
| 0x03 | Undervoltage |
| 0x04 | Overload |
| 0x05 | Overtemperature |
| 0x06 | Motor Overload |
| 0x07 | External Fault |
| 0x08 | Communication Error |
| 0x09 | Phase Loss |

## Safety Considerations

⚠️ **IMPORTANT SAFETY WARNINGS**

1. **Electrical Safety**
   - Work with qualified personnel when dealing with AC power
   - Follow all local electrical codes
   - Ensure proper grounding
   - Use appropriate circuit protection

2. **Motor Control**
   - Verify motor compatibility with inverter
   - Set appropriate frequency limits
   - Implement emergency stop procedures
   - Test in safe conditions before production use

3. **Communication**
   - Secure MQTT broker access
   - Use strong passwords
   - Consider TLS/SSL for production
   - Implement access control

## License

This project is released under the MIT License.

## Support and Contribution

For issues, questions, or contributions:
- Open an issue on GitHub
- Submit pull requests for improvements
- Share your implementation experiences

## Version History

### v1.0.0 (2026-01-08)
- Initial release
- Full VPC M0701S support
- Multi-inverter capability
- MQTT integration
- Web control panel
- Comprehensive testing
- Documentation

## Acknowledgments

- ModbusMaster library by Doc Walker
- PubSubClient library by Nick O'Leary
- ArduinoJson library by Benoit Blanchon
- ESP32 Arduino Core by Espressif

---

**Note**: This controller is designed for the VPC M0701S inverter. While the Modbus register map is based on common VFD standards, always verify with your specific inverter's documentation for accurate register addresses and scaling factors.
