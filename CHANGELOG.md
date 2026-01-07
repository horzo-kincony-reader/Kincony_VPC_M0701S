# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-01-07

### Added
- Initial release of VPC-M0701S Inverter Reader
- Core `VPC_M0701S_Reader` class for MODBUS RTU communication
- YAML-based configuration system
- Support for reading inverter status (frequency, current, voltage, DC bus voltage)
- Support for control operations (set frequency, start/stop)
- Context manager support for automatic connection management
- Example scripts:
  - `example_usage.py` - Basic usage demonstration
  - `monitor.py` - Continuous status monitoring
  - `test_connection.py` - Connection testing utility
- Comprehensive documentation:
  - README.md with installation and usage guide
  - DEVELOPER.md with architecture and extension guide
  - QUICKSTART.md for quick setup
- Package setup with setup.py for pip installation
- MIT License
- Python 3.7+ support

### Features
- MODBUS RTU protocol over RS485
- Configurable serial port settings
- Configurable register map
- Error handling and logging
- Type hints for better code clarity

### Dependencies
- pymodbus >= 3.6.0
- pyserial >= 3.5
- pyyaml >= 6.0

### Security
- No known vulnerabilities in dependencies
- CodeQL security analysis passed
- No hardcoded credentials or secrets
