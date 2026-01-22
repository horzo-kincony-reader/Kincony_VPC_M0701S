# Kincony_VPC_M0701S

KC868-A16 firmware with multi-inverter support (ME300 + VPC-M0701S)

## Overview

This firmware extends the KC868-A16 platform to support both ME300 and VPC-M0701S inverters via Modbus RTU (RS485). Each of the 6 SIDs (slave IDs) can be independently configured as either ME300 or VPC-M0701S type.

## Features

- **Multi-SID Support**: 6 independent inverter slots (SID 1-6)
- **Per-SID Type Configuration**: Each SID can be configured as ME300 or VPC-M0701S
- **VPC-M0701S Support**: 
  - Configurable Modbus address base (4xxxx notation or raw)
  - Adjustable scaling factors for frequency, current, voltage, temperature
  - Function code selection (FC03, FC04, or auto-fallback)
  - Status reading, frequency control, start/stop, fault clearing
- **ModbusTCP Server**: Exposes all inverters via ModbusTCP (port 502)
- **Web Interface**: Configure inverters, view status, send commands
- **MQTT Integration**: Publish telemetry and receive control commands
- **Persistent Configuration**: Settings saved in NVS (non-volatile storage)

## Hardware

- **Board**: KC868-A16 (ESP32-based)
- **RS485 Pins**: RX=16, TX=13
- **Communication**: Modbus RTU over RS485

## VPC-M0701S Configuration

### Register Mapping

VPC registers are mapped to ModbusTCP Ireg/Hreg slots per SID:

**Input Registers (Telemetry)** - Base = (SID-1) * 100:
- `Base+0`: Fault/alarm code (VPC reg 40189)
- `Base+1`: Running status + direction (VPC reg 40180)
- `Base+2`: Set frequency (VPC reg 40181)
- `Base+3`: Running frequency (VPC reg 40182)
- `Base+4`: Running current (VPC reg 40183)
- `Base+5`: DC bus voltage (VPC reg 40184)
- `Base+6`: Temperature (VPC reg 40185)

**Holding Registers (Control)** - Base = (SID-1) * 100:
- `Base+0`: Control word (maps to VPC P103)
- `Base+1`: Set frequency raw (maps to VPC P102)
- `Base+2`: Flags (bit 0x0002 = fault reset request)

### Configuration Parameters

Access via web UI: `http://<device-ip>/inverter_master/config`

**Per-SID Settings**:
- **Type**: `me300` or `vpc` (VPC-M0701S)
- **VPC Addr**: Modbus slave address (1-247), default = SID number
- **Addr Base**: Address offset for 4xxxx notation (default 40001) or 0 for raw addresses
- **Freq Div**: Frequency scaling divisor (default 100, e.g., 5000/100 = 50.00 Hz)
- **Curr Div**: Current scaling divisor (default 100)
- **Volt Div**: Voltage scaling divisor (default 10, e.g., 3600/10 = 360.0 V)
- **Temp Div**: Temperature scaling divisor (default 1)
- **Read FC**: Function code for reads (0=auto, 3=FC03, 4=FC04)

### Web Interface

- **Main Panel**: `http://<device-ip>/`
- **Inverter Multi-SID**: `http://<device-ip>/inverter_master`
- **Inverter Config**: `http://<device-ip>/inverter_master/config`
- **VPC Direct Control**: `http://<device-ip>/vpc` (legacy, single VPC)

### MQTT Topics

**Per-SID Telemetry** (published):
```
KINCONY/INVERTER/<sid>/state
```

**Per-SID Control** (subscribe):
```
KINCONY/INVERTER/<sid>/set
```

**Legacy VPC Control** (subscribe):
```
KINCONY/VPC/set
Payload: {"cmd":"start|stop|setf|reset","freq":50.00}
```

## Configuration Workflow

1. **Access Web UI**: Navigate to `http://<device-ip>/inverter_master/config`
2. **Select Type**: For each SID, choose ME300 or VPC-M0701S
3. **Configure VPC Parameters**: Set address, base, and scaling divisors
4. **Save**: Configuration persists across reboots
5. **Monitor**: View status at `/inverter_master` or via MQTT

## Notes

- **Backward Compatibility**: Existing ME300 functionality is preserved
- **Mixed Configuration**: You can mix ME300 and VPC inverters (e.g., SID1-3 = ME300, SID4-6 = VPC)
- **Address Base**: Use 40001 for standard 4xxxx notation or 0 for raw Modbus addresses
- **Scaling**: If you don't know exact scaling, start with defaults and adjust based on observed values
- **Authentication**: Web UI requires digest auth (default: admin/darol177)

## Development

- Main sketch: `Kincony_VPC_M0701S.ino`
- AutoMultiInverter: `inverter_master_append_multi_autodetect_v21a1711_Version4.ino`
- VPC Modbus library: `VPC_Modbus.h` / `VPC_Modbus.cpp`
- Register definitions: `include/vpc_m0701s_regs.h`, `include/me300_regs.h`

## License

See repository license file.

