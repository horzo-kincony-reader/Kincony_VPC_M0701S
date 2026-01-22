/*
 * VPC-M0701S Modbus RTU Register Definitions
 * Based on VC_M0701S_REGISTER.csv
 * 
 * Address Convention:
 * - CSV uses 4xxxx notation (e.g., 40180 for holding register 179)
 * - Modbus function codes: FC03 (read holding), FC04 (read input)
 * - Base address: configurable (default: 40001 offset for 4xxxx style)
 * 
 * Integration with ModbusTCP:
 * Each VPC inverter is mapped to a ModbusTCP register range based on SID:
 *   - IREG base = (SID-1) * 100 (input registers for telemetry)
 *   - HREG base = (SID-1) * 100 (holding registers for control)
 * 
 * Example for SID=1:
 *   - Read telemetry from IREG 0-6
 *   - Write control to HREG 0-2
 * 
 * Scaling:
 * Raw values from VPC are scaled using configurable divisors:
 *   - Frequency: raw / freq_div (default 100) = Hz
 *   - Current: raw / curr_div (default 100) = A
 *   - Voltage: raw / volt_div (default 10) = V
 *   - Temperature: raw / temp_div (default 1) = °C
 */
#pragma once
#include <stdint.h>

namespace VPC_M0701S {

// ===== Status Registers (Read) =====
// These are typically read using FC03 or FC04
static constexpr uint16_t RUNNING_STATUS        = 40180;  // Running status + direction bitfields
static constexpr uint16_t CURRENT_SET_FREQ      = 40181;  // Current set frequency (raw, needs scaling)
static constexpr uint16_t RUNNING_FREQ          = 40182;  // Running frequency (raw, needs scaling)
static constexpr uint16_t RUNNING_CURRENT       = 40183;  // Running current (raw, needs scaling)
static constexpr uint16_t RUNNING_VOLTAGE_DCBUS = 40184;  // Running voltage / DC bus (raw, needs scaling)
static constexpr uint16_t TEMPERATURE           = 40185;  // Temperature (raw, needs scaling)
static constexpr uint16_t FAULT_ALARMS          = 40189;  // Fault/alarm codes
static constexpr uint16_t FAULT_CLEAR_WRITE     = 40198;  // Write >0 to clear fault

// ===== Parameter Registers (Read/Write) =====
// P102: 485 frequency set value (typically at parameter base + 102)
// P103: 485 operation setting bits (control word)
static constexpr uint16_t PARAM_BASE            = 40000;  // Parameter P00 base address
static constexpr uint16_t PARAM_485_FREQ_SET    = PARAM_BASE + 102;  // P102
static constexpr uint16_t PARAM_485_OPERATION   = PARAM_BASE + 103;  // P103

// ===== Default Scaling Factors =====
// User doesn't know exact scaling, so these are configurable defaults
static constexpr uint16_t DEFAULT_FREQ_DIV      = 100;    // Frequency divisor (e.g., 5000 raw = 50.00 Hz)
static constexpr uint16_t DEFAULT_CURR_DIV      = 100;    // Current divisor
static constexpr uint16_t DEFAULT_VOLT_DIV      = 10;     // Voltage divisor (e.g., 3600 raw = 360.0 V)
static constexpr uint16_t DEFAULT_TEMP_DIV      = 1;      // Temperature divisor

// ===== Address Conversion Helpers =====
// Convert 4xxxx notation to 0-based Modbus address
// E.g., 40180 -> address 179 (offset by 40001)
inline uint16_t to_modbus_addr(uint16_t addr_4xxxx, uint16_t base_offset = 40001) {
    return (addr_4xxxx >= base_offset) ? (addr_4xxxx - base_offset) : addr_4xxxx;
}

// ModbusTCP register layout per SID (matching ME300 layout)
// Base offset per SID = (sid-1) * 100
// IREG mapping (input registers for status/telemetry):
enum IregOffset {
    IREG_FAULT_CODE     = 0,   // Fault/alarm code (40189)
    IREG_STATUS_DIR     = 1,   // Running status + direction (40180)
    IREG_SET_FREQ       = 2,   // Set frequency (40181)
    IREG_RUNNING_FREQ   = 3,   // Running frequency (40182)
    IREG_RUNNING_CURR   = 4,   // Running current (40183)
    IREG_DC_BUS_VOLT    = 5,   // DC bus voltage (40184)
    IREG_TEMPERATURE    = 6    // Temperature (40185)
};

// HREG mapping (holding registers for control/setpoints):
enum HregOffset {
    HREG_CONTROL_WORD   = 0,   // Control word (maps to P103 operation bits)
    HREG_SET_FREQ       = 1,   // Set frequency (maps to P102)
    HREG_FLAGS          = 2    // Bit 0x0002 = reset/clear fault request
};

} // namespace VPC_M0701S
