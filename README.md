# Kincony VPC-M0701S Inverter Reader

Python library for communicating with Kincony VPC-M0701S Chinese frequency inverters via MODBUS RTU protocol over RS485.

## Overview

This project provides a Python-based solution for reading status data and controlling the VPC-M0701S (also known as VFC-M0701S) frequency inverter. The inverter is a Chinese-made variable frequency drive (VFD) commonly used for controlling three-phase motors in industrial and home applications.

## Features

- **MODBUS RTU Communication**: Full support for RS485 MODBUS protocol
- **Status Monitoring**: Read output frequency, current, voltage, and DC bus voltage
- **Inverter Control**: Set frequency setpoints, start/stop commands
- **Easy Configuration**: YAML-based configuration file
- **Context Manager Support**: Automatic connection management
- **Logging**: Comprehensive logging for debugging and monitoring

## Requirements

- Python 3.7 or higher
- RS485 to USB adapter (for connection to the inverter)
- VPC-M0701S inverter with RS485 interface

## Installation

1. Clone this repository:
```bash
git clone https://github.com/horzo-kincony-reader/Kincony_VPC_M0701S.git
cd Kincony_VPC_M0701S
```

2. Install required dependencies:
```bash
pip install -r requirements.txt
```

## Configuration

1. Copy and edit the `config.yml` file to match your setup:
```yaml
inverter:
  port: /dev/ttyUSB0  # COM port on Windows (e.g., COM3)
  baudrate: 9600
  slave_id: 1
```

2. Adjust the serial port settings based on your RS485 adapter and inverter configuration.

## Usage

### Basic Example

```python
from vpc_reader import VPC_M0701S_Reader

# Use context manager for automatic connection/disconnection
with VPC_M0701S_Reader(config_path="config.yml") as reader:
    # Read current status
    status = reader.read_status()
    print(f"Output Frequency: {status['output_frequency']} Hz")
    print(f"Output Current: {status['output_current']} A")
    
    # Set frequency and start inverter
    reader.set_frequency(50.0)
    reader.start()
```

### Example Scripts

The repository includes ready-to-use example scripts:

1. **example_usage.py** - Basic usage demonstration
```bash
python example_usage.py
```

2. **monitor.py** - Continuous monitoring of inverter status
```bash
python monitor.py
```

## API Reference

### VPC_M0701S_Reader Class

#### Connection Methods
- `connect()` - Establish connection to the inverter
- `disconnect()` - Close connection
- Context manager support with `__enter__` and `__exit__`

#### Read Methods
- `read_output_frequency()` - Read output frequency in Hz
- `read_output_current()` - Read output current in Amperes
- `read_output_voltage()` - Read output voltage in Volts
- `read_dc_bus_voltage()` - Read DC bus voltage in Volts
- `read_status()` - Read all status values at once

#### Control Methods
- `set_frequency(frequency)` - Set target frequency in Hz
- `start()` - Start the inverter
- `stop()` - Stop the inverter

#### Low-level Methods
- `read_register(address, count)` - Read raw MODBUS registers
- `write_register(address, value)` - Write to MODBUS registers

## Hardware Connection

Connect your computer to the inverter using an RS485 to USB adapter:

```
Computer (USB) <-> RS485 Adapter <-> Inverter (RS485 A/B terminals)
```

Typical RS485 wiring:
- A+ (or D+) to inverter's A+ terminal
- B- (or D-) to inverter's B- terminal
- GND (optional, for common ground)

## Register Map

The default register addresses are configured in `config.yml`. These may need adjustment based on your specific inverter model:

- **0x0000**: Output frequency (read-only)
- **0x0001**: Output current (read-only)
- **0x0002**: Output voltage (read-only)
- **0x0003**: DC bus voltage (read-only)
- **0x1000**: Frequency setpoint (read-write)
- **0x1001**: Run command (read-write)

**Note**: Register addresses and scaling factors may vary between different inverter models. Consult your inverter's MODBUS protocol documentation for exact specifications.

## Troubleshooting

### Connection Issues
- Verify the correct COM port or device path
- Check RS485 wiring (A+ and B- terminals)
- Ensure the inverter's MODBUS slave ID matches configuration
- Verify baud rate and serial settings

### Reading Errors
- Check if the inverter is powered on
- Verify register addresses match your inverter model
- Enable debug logging to see detailed MODBUS communication

### Scaling Issues
If values seem incorrect, adjust the scaling factors in the reader methods (e.g., division by 100 or 10) based on your inverter's protocol documentation.

## Project Structure

```
Kincony_VPC_M0701S/
├── vpc_reader/              # Main library package
│   ├── __init__.py
│   └── vpc_m0701s_reader.py # Reader class implementation
├── config.yml               # Configuration file
├── example_usage.py         # Basic usage example
├── monitor.py              # Continuous monitoring script
├── requirements.txt        # Python dependencies
├── .gitignore             # Git ignore file
└── README.md              # This file
```

## Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## License

This project is licensed under the MIT License.

## Acknowledgments

- Based on the MODBUS RTU protocol specification
- Uses pymodbus library for MODBUS communication
- Designed for Kincony VPC-M0701S (VFC-M0701S) frequency inverters

## Disclaimer

This software is provided as-is. Always ensure proper electrical safety when working with inverters and industrial equipment. The authors are not responsible for any damage or injury resulting from the use of this software.
