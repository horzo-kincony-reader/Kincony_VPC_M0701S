#ifndef VPC_MODBUS_H
#define VPC_MODBUS_H

#include <Arduino.h>
#include <ModbusMaster.h>
#include "include/vpc_m0701s_regs.h"

/*
 * VPC-M0701S Modbus RTU Interface (Per-SID support)
 * 
 * This module provides per-SID VPC inverter operations without maintaining
 * a global ModbusMaster instance. The ModbusMaster instance should be 
 * managed by the caller (e.g., AutoMultiInverter).
 * 
 * Address Convention:
 * - Supports both raw Modbus addresses and 4xxxx notation
 * - addr_base: offset to convert 4xxxx addresses (default 40001)
 * - Scaling factors configurable per deployment
 */

// Per-SID VPC Configuration
struct VPCConfig {
    uint8_t  inverter_addr;        // Modbus slave address (1-247)
    uint16_t addr_base;            // Address offset (40001 for 4xxxx, or 0 for raw)
    uint16_t freq_div;             // Frequency scaling divisor (default 100)
    uint16_t curr_div;             // Current scaling divisor (default 100)
    uint16_t volt_div;             // Voltage scaling divisor (default 10)
    uint16_t temp_div;             // Temperature scaling divisor (default 1)
    uint8_t  read_fc;              // Function code for reads: 3=FC03, 4=FC04, 0=auto-fallback
    
    // Default constructor
    VPCConfig() : 
        inverter_addr(1),
        addr_base(40001),
        freq_div(VPC_M0701S::DEFAULT_FREQ_DIV),
        curr_div(VPC_M0701S::DEFAULT_CURR_DIV),
        volt_div(VPC_M0701S::DEFAULT_VOLT_DIV),
        temp_div(VPC_M0701S::DEFAULT_TEMP_DIV),
        read_fc(0) {}
};

// VPC Telemetry Data (scaled values)
struct VPCTelemetry {
    uint16_t fault_code;           // Raw fault/alarm code
    uint16_t status_dir;           // Running status + direction bitfield
    uint16_t set_freq_raw;         // Set frequency (raw)
    uint16_t running_freq_raw;     // Running frequency (raw)
    uint16_t running_curr_raw;     // Running current (raw)
    uint16_t dc_bus_volt_raw;      // DC bus voltage (raw)
    uint16_t temperature_raw;      // Temperature (raw)
    
    // Scaled values (floats)
    float    set_freq_hz;
    float    running_freq_hz;
    float    running_curr_a;
    float    dc_bus_volt_v;
    float    temperature_c;
};

// ===== Per-SID VPC Operations =====

// Read VPC status registers into telemetry structure
// Returns true on success, false on Modbus error
bool VPC_readTelemetry(ModbusMaster& mb, const VPCConfig& cfg, VPCTelemetry& telem);

// Write VPC control word (P103 - operation bits)
// control_word: bits for start/stop/direction/etc.
bool VPC_writeControlWord(ModbusMaster& mb, const VPCConfig& cfg, uint16_t control_word);

// Write VPC set frequency (P102 - frequency setpoint)
// freq_raw: raw frequency value (e.g., 5000 for 50.00 Hz if div=100)
bool VPC_writeSetFrequency(ModbusMaster& mb, const VPCConfig& cfg, uint16_t freq_raw);

// Clear VPC fault (write >0 to fault clear register)
bool VPC_clearFault(ModbusMaster& mb, const VPCConfig& cfg);

// ===== Legacy Global API (for backward compatibility with existing VPC endpoints) =====
// These use a shared ModbusMaster instance passed by reference
extern ModbusMaster vpc_legacy_node;
void VPC_init(Stream &serial, uint8_t modbusAddress);
bool VPC_readStatus();
bool VPC_setFrequency(float freq);
bool VPC_start();
bool VPC_stop();
bool VPC_clearFault();
void VPC_debugStatus();

#endif // VPC_MODBUS_H