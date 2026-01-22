# Kincony KC868-A16 Firmware Development for Delta ME300 and VPC M0701S

## Project Overview
This firmware project aims to provide robust support for the Kincony KC868-A16 controller, specifically tailored for Delta ME300 inverters and VPC M0701S devices. The goal is to enable seamless communication and control functionalities that enhance energy management and monitoring.

## Hardware Notes
- **RS485 Pins**: Use pins 16 (TX) and 13 (RX) for RS485 communication.
- **Analog Inputs**: Ensure connections to the appropriate analog input pins for sensor integration and data acquisition.

## Build Instructions
### Arduino IDE 2.x
1. Open Arduino IDE 2.x and install the necessary libraries for RS485 communication and MQTT support.
2. Download the firmware source code.
3. Open the project in Arduino IDE.
4. Select the appropriate board (Kincony KC868-A16) and configure the settings as required.
5. Compile the code and upload to the device.

### PlatformIO
1. Ensure PlatformIO is installed in your IDE.
2. Create a new project and add the firmware source code to the `src` directory.
3. Configure the `platformio.ini` file to include necessary libraries and dependencies.
4. Build the project using the PlatformIO commands and upload to the device.

## Run/Test Checklist
- Verify power supply and connections.
- Test communication with the Delta ME300 inverter via RS485.
- Validate analog inputs by reading and logging values. 
- Check MQTT topic subscriptions and publish intervals.
- Test HTTP endpoints for remote access and control.

## MQTT Topics & HTTP Endpoints Summary
- **MQTT Topics**:
  - `home/inverter/status`
  - `home/inverter/control`

- **HTTP Endpoints**:
  - `GET /api/inverter/status`
  - `POST /api/inverter/control`

## Git Workflow for a Single Developer
1. Create a new branch for each feature or bug fix using the naming convention `feature/[feature-name]`.
2. Make changes locally and commit them with clear, descriptive messages.
3. Regularly pull from the main branch to stay up-to-date with any changes.
4. Once completed, create a pull request with a thorough description of the changes.
5. Merge into the main branch after review. 
6. Tag releases as necessary for version control practices.