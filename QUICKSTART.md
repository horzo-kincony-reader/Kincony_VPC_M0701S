# Quick Start Guide

Get started with the VPC-M0701S Inverter Reader in 5 minutes!

## Prerequisites

- Python 3.7 or higher
- RS485 to USB adapter
- VPC-M0701S inverter

## Step 1: Install

```bash
git clone https://github.com/horzo-kincony-reader/Kincony_VPC_M0701S.git
cd Kincony_VPC_M0701S
pip install -r requirements.txt
```

Or install as a package:
```bash
pip install -e .
```

## Step 2: Configure

Edit `config.yml` to match your setup:

```yaml
inverter:
  port: /dev/ttyUSB0  # Change to your port (COM3 on Windows)
  baudrate: 9600
  slave_id: 1
```

**Finding your port:**
- Linux: Usually `/dev/ttyUSB0` or `/dev/ttyUSB1`
- Windows: Check Device Manager for COM port (e.g., `COM3`)
- macOS: Usually `/dev/tty.usbserial-*`

## Step 3: Test Connection

```bash
python test_connection.py
```

If successful, you should see:
```
✓ Configuration loaded successfully
✓ Connection established successfully
✓ Read operation successful
CONNECTION TEST PASSED
```

## Step 4: Run Examples

### Monitor inverter status:
```bash
python monitor.py
```

### Try the basic example:
```bash
python example_usage.py
```

## Step 5: Use in Your Code

```python
from vpc_reader import VPC_M0701S_Reader

with VPC_M0701S_Reader() as reader:
    status = reader.read_status()
    print(f"Frequency: {status['output_frequency']} Hz")
```

## Troubleshooting

### "Cannot open port"
```bash
# Linux - Add user to dialout group:
sudo usermod -a -G dialout $USER
# Log out and back in
```

### "Failed to connect"
1. Check RS485 wiring (A+ and B- terminals)
2. Verify inverter is powered on
3. Confirm baud rate in config matches inverter settings
4. Check slave_id in config matches inverter

### "Permission denied"
```bash
# Linux - Give permissions temporarily:
sudo chmod 666 /dev/ttyUSB0
```

## Next Steps

- Read [README.md](README.md) for detailed documentation
- Check [DEVELOPER.md](DEVELOPER.md) for extending the library
- Customize register addresses in `config.yml` for your inverter model

## Quick Reference

**Read status:**
```python
reader.read_output_frequency()  # Hz
reader.read_output_current()    # Amperes
reader.read_output_voltage()    # Volts
reader.read_dc_bus_voltage()    # Volts
reader.read_status()            # All values
```

**Control (use with caution!):**
```python
reader.set_frequency(50.0)  # Set 50 Hz
reader.start()              # Start inverter
reader.stop()               # Stop inverter
```

## Safety Warning

⚠️ **Always ensure proper electrical safety when working with inverters!**
- Disconnect power before wiring
- Follow manufacturer's safety guidelines
- Test control commands carefully
- Monitor the system during operation

## Getting Help

- Check the [README.md](README.md) for detailed documentation
- Review [DEVELOPER.md](DEVELOPER.md) for technical details
- Open an issue on GitHub for bugs or questions

Happy inverter controlling! 🎛️
