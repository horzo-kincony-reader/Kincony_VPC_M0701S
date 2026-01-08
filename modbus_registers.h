#ifndef MODBUS_REGISTERS_H
#define MODBUS_REGISTERS_H

// VPC M0701S Modbus Register Mappings
// Based on typical VFD (Variable Frequency Drive) register layouts

// Holding Registers (Read/Write) - Function Code 0x03/0x06/0x10
#define REG_CONTROL_COMMAND       0x2000  // Control word: Start/Stop/Reset
#define REG_FREQUENCY_SETPOINT    0x2001  // Target frequency (0.01 Hz resolution)
#define REG_ACCELERATION_TIME     0x2002  // Acceleration time (0.1s resolution)
#define REG_DECELERATION_TIME     0x2003  // Deceleration time (0.1s resolution)
#define REG_UPPER_LIMIT_FREQ      0x2004  // Maximum frequency limit
#define REG_LOWER_LIMIT_FREQ      0x2005  // Minimum frequency limit
#define REG_MOTOR_RATED_CURRENT   0x2006  // Motor rated current (0.1A resolution)
#define REG_MOTOR_RATED_VOLTAGE   0x2007  // Motor rated voltage (1V resolution)
#define REG_MOTOR_RATED_POWER     0x2008  // Motor rated power (0.1kW resolution)
#define REG_MOTOR_POLES           0x2009  // Number of motor poles

// Input Registers (Read Only) - Function Code 0x04
#define REG_RUNNING_STATUS        0x3000  // Running status word
#define REG_OUTPUT_FREQUENCY      0x3001  // Current output frequency (0.01 Hz)
#define REG_OUTPUT_CURRENT        0x3002  // Output current (0.1A resolution)
#define REG_OUTPUT_VOLTAGE        0x3003  // Output voltage (1V resolution)
#define REG_BUS_VOLTAGE           0x3004  // DC bus voltage (1V resolution)
#define REG_OUTPUT_POWER          0x3005  // Output power (0.1kW resolution)
#define REG_OUTPUT_TORQUE         0x3006  // Output torque (0.1% resolution)
#define REG_MOTOR_SPEED           0x3007  // Motor speed (1 RPM resolution)
#define REG_INVERTER_TEMP         0x3008  // Inverter temperature (0.1°C resolution)
#define REG_FAULT_CODE            0x3009  // Current fault code
#define REG_RUNNING_TIME          0x300A  // Total running time (hours)
#define REG_INPUT_TERMINAL        0x300B  // Digital input terminal status

// Control Command Bits (REG_CONTROL_COMMAND)
#define CMD_STOP                  0x0000
#define CMD_START_FORWARD         0x0001
#define CMD_START_REVERSE         0x0002
#define CMD_JOG_FORWARD           0x0003
#define CMD_JOG_REVERSE           0x0004
#define CMD_RESET_FAULT           0x0007
#define CMD_EMERGENCY_STOP        0x0008

// Status Bits (REG_RUNNING_STATUS)
#define STATUS_READY              0x0001
#define STATUS_RUNNING            0x0002
#define STATUS_FORWARD            0x0004
#define STATUS_REVERSE            0x0008
#define STATUS_FAULT              0x0010
#define STATUS_WARNING            0x0020
#define STATUS_AT_FREQUENCY       0x0040
#define STATUS_OVERLOAD           0x0080

// Fault Codes
#define FAULT_NONE                0x0000
#define FAULT_OVERCURRENT         0x0001
#define FAULT_OVERVOLTAGE         0x0002
#define FAULT_UNDERVOLTAGE        0x0003
#define FAULT_OVERLOAD            0x0004
#define FAULT_OVERTEMP            0x0005
#define FAULT_MOTOR_OVERLOAD      0x0006
#define FAULT_EXTERNAL            0x0007
#define FAULT_COMM_ERROR          0x0008
#define FAULT_PHASE_LOSS          0x0009

// Conversion macros
#define FREQ_TO_REG(freq)         ((uint16_t)((freq) * 100))
#define REG_TO_FREQ(reg)          ((float)(reg) / 100.0)
#define CURRENT_TO_REG(curr)      ((uint16_t)((curr) * 10))
#define REG_TO_CURRENT(reg)       ((float)(reg) / 10.0)
#define TEMP_TO_REG(temp)         ((uint16_t)((temp) * 10))
#define REG_TO_TEMP(reg)          ((float)(reg) / 10.0)
#define POWER_TO_REG(power)       ((uint16_t)((power) * 10))
#define REG_TO_POWER(reg)         ((float)(reg) / 10.0)

#endif // MODBUS_REGISTERS_H
