# Developer Documentation

## Project Overview

This project provides a Python library for interfacing with the Kincony VPC-M0701S frequency inverter via MODBUS RTU protocol.

## Architecture

```
vpc_reader/
├── __init__.py              # Package initialization and exports
└── vpc_m0701s_reader.py     # Main reader class implementation
```

## Class Structure

### VPC_M0701S_Reader

The main class handling all communication with the inverter.

**Key Components:**
- Configuration management (YAML-based)
- MODBUS RTU client (using pymodbus)
- Register read/write methods
- High-level convenience methods for common operations

**Design Patterns:**
- Context manager (for automatic connection/disconnection)
- Configuration pattern (YAML-based settings)
- Logging integration (Python logging module)

## MODBUS Protocol Details

### Communication Parameters
- Protocol: MODBUS RTU
- Interface: RS485
- Default Baud Rate: 9600
- Data Bits: 8
- Parity: None
- Stop Bits: 1

### Register Types
The VPC-M0701S uses holding registers (function code 0x03 for read, 0x06 for write).

### Data Scaling
Values read from registers often need scaling:
- Frequency: register_value / 100.0 (Hz)
- Current: register_value / 100.0 (A)
- Voltage: register_value / 10.0 (V)

**Important**: These scaling factors are based on common VFD conventions and may need adjustment for your specific inverter model.

## Configuration

### config.yml Structure

```yaml
inverter:
  # Serial port settings
  port: /dev/ttyUSB0
  baudrate: 9600
  bytesize: 8
  parity: N
  stopbits: 1
  timeout: 1.0
  
  # MODBUS settings
  slave_id: 1
  
  # Register map
  registers:
    output_frequency: 0x0000
    # ... more registers
```

## Extending the Library

### Adding New Register Reads

1. Add the register address to `config.yml`:
```yaml
registers:
  motor_temperature: 0x0010
```

2. Add a convenience method in `VPC_M0701S_Reader`:
```python
def read_motor_temperature(self) -> Optional[float]:
    registers = self.read_register(self.registers['motor_temperature'])
    if registers:
        return registers[0] / 10.0  # Adjust scaling as needed
    return None
```

### Adding Control Functions

Follow the pattern of `start()` and `stop()` methods:

```python
def set_acceleration_time(self, seconds: float) -> bool:
    """Set acceleration time in seconds."""
    reg_value = int(seconds * 10)  # Adjust scaling
    return self.write_register(self.registers['acceleration_time'], reg_value)
```

## Testing

### Manual Testing

Since this library requires physical hardware, automated testing is limited. Test manually:

1. **Connection Test**: Verify the reader can connect to the inverter
2. **Read Test**: Verify status values are read correctly
3. **Write Test**: Verify control commands work (use caution)

### Without Hardware

You can test the code structure without hardware:
```python
reader = VPC_M0701S_Reader(config_path="config.yml")
# Don't call connect() - just test instantiation and config loading
```

## Error Handling

The library uses the following error handling strategy:

1. **Connection Errors**: Return False from `connect()`
2. **Read Errors**: Return None from read methods
3. **Write Errors**: Return False from write methods
4. **Configuration Errors**: Raise exceptions during initialization

All errors are logged using Python's logging module.

## Dependencies

- **pymodbus**: MODBUS protocol implementation
- **pyserial**: Serial port communication
- **pyyaml**: YAML configuration parsing

## Common Issues and Solutions

### Issue: "Cannot open port"
**Solution**: Check that the port name is correct and you have permissions to access it.

On Linux:
```bash
sudo usermod -a -G dialout $USER
# Log out and back in
```

### Issue: "No response from inverter"
**Solutions**:
- Verify RS485 wiring (A+ and B- correctly connected)
- Check that inverter is powered on
- Verify slave_id matches inverter configuration
- Check baud rate matches inverter settings

### Issue: "Values seem incorrect"
**Solution**: Adjust scaling factors in the read methods based on your inverter's protocol documentation.

## Contributing

When contributing:
1. Follow PEP 8 style guidelines
2. Add docstrings to all public methods
3. Use type hints for function parameters and return values
4. Log errors appropriately
5. Update documentation

## Future Enhancements

Potential improvements:
- Support for multiple inverters on the same bus
- Batch read operations for efficiency
- Async/await support for non-blocking operations
- Web interface for monitoring
- Data logging and graphing capabilities
- Protocol auto-detection
- Register map auto-discovery
