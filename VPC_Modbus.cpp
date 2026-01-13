#include "VPC_Modbus.h"

// ===== Per-SID VPC Operations =====

// Helper: convert 4xxxx address to Modbus address using config
static inline uint16_t vpc_to_modbus(uint16_t addr_4xxxx, uint16_t addr_base) {
    return (addr_4xxxx >= addr_base) ? (addr_4xxxx - addr_base) : addr_4xxxx;
}

// Read VPC status registers into telemetry structure
bool VPC_readTelemetry(ModbusMaster& mb, const VPCConfig& cfg, VPCTelemetry& telem) {
    // Set slave address
    mb.begin(cfg.inverter_addr, mb.getStream());
    
    // Read status registers starting from RUNNING_STATUS (40180)
    // We need registers: 40180, 40181, 40182, 40183, 40184, 40185, skip to 40189
    uint16_t base_addr = vpc_to_modbus(VPC_M0701S::RUNNING_STATUS, cfg.addr_base);
    
    uint8_t result;
    
    // Try FC03 (read holding registers) or FC04 (read input registers)
    if (cfg.read_fc == 4) {
        result = mb.readInputRegisters(base_addr, 6);
    } else if (cfg.read_fc == 3) {
        result = mb.readHoldingRegisters(base_addr, 6);
    } else {
        // Auto-fallback: try FC03 first, then FC04
        result = mb.readHoldingRegisters(base_addr, 6);
        if (result != mb.ku8MBSuccess) {
            result = mb.readInputRegisters(base_addr, 6);
        }
    }
    
    if (result != mb.ku8MBSuccess) {
        return false;
    }
    
    // Extract values from response buffer
    telem.status_dir        = mb.getResponseBuffer(0);  // 40180
    telem.set_freq_raw      = mb.getResponseBuffer(1);  // 40181
    telem.running_freq_raw  = mb.getResponseBuffer(2);  // 40182
    telem.running_curr_raw  = mb.getResponseBuffer(3);  // 40183
    telem.dc_bus_volt_raw   = mb.getResponseBuffer(4);  // 40184
    telem.temperature_raw   = mb.getResponseBuffer(5);  // 40185
    
    // Read fault alarms (40189) - separate read
    uint16_t fault_addr = vpc_to_modbus(VPC_M0701S::FAULT_ALARMS, cfg.addr_base);
    if (cfg.read_fc == 4) {
        result = mb.readInputRegisters(fault_addr, 1);
    } else if (cfg.read_fc == 3) {
        result = mb.readHoldingRegisters(fault_addr, 1);
    } else {
        result = mb.readHoldingRegisters(fault_addr, 1);
        if (result != mb.ku8MBSuccess) {
            result = mb.readInputRegisters(fault_addr, 1);
        }
    }
    
    if (result == mb.ku8MBSuccess) {
        telem.fault_code = mb.getResponseBuffer(0);
    } else {
        telem.fault_code = 0;  // Default if read fails
    }
    
    // Apply scaling
    telem.set_freq_hz     = (float)telem.set_freq_raw / (float)cfg.freq_div;
    telem.running_freq_hz = (float)telem.running_freq_raw / (float)cfg.freq_div;
    telem.running_curr_a  = (float)telem.running_curr_raw / (float)cfg.curr_div;
    telem.dc_bus_volt_v   = (float)telem.dc_bus_volt_raw / (float)cfg.volt_div;
    telem.temperature_c   = (float)telem.temperature_raw / (float)cfg.temp_div;
    
    return true;
}

// Write VPC control word (P103)
bool VPC_writeControlWord(ModbusMaster& mb, const VPCConfig& cfg, uint16_t control_word) {
    mb.begin(cfg.inverter_addr, mb.getStream());
    uint16_t addr = vpc_to_modbus(VPC_M0701S::PARAM_485_OPERATION, cfg.addr_base);
    uint8_t result = mb.writeSingleRegister(addr, control_word);
    return (result == mb.ku8MBSuccess);
}

// Write VPC set frequency (P102)
bool VPC_writeSetFrequency(ModbusMaster& mb, const VPCConfig& cfg, uint16_t freq_raw) {
    mb.begin(cfg.inverter_addr, mb.getStream());
    uint16_t addr = vpc_to_modbus(VPC_M0701S::PARAM_485_FREQ_SET, cfg.addr_base);
    uint8_t result = mb.writeSingleRegister(addr, freq_raw);
    return (result == mb.ku8MBSuccess);
}

// Clear VPC fault
bool VPC_clearFault(ModbusMaster& mb, const VPCConfig& cfg) {
    mb.begin(cfg.inverter_addr, mb.getStream());
    uint16_t addr = vpc_to_modbus(VPC_M0701S::FAULT_CLEAR_WRITE, cfg.addr_base);
    uint8_t result = mb.writeSingleRegister(addr, 1);  // Write >0 to clear
    return (result == mb.ku8MBSuccess);
}

// ===== Legacy Global API (for backward compatibility) =====

ModbusMaster vpc_legacy_node;
static uint8_t legacy_deviceAddress = 1;
static VPCConfig legacy_cfg;

void VPC_init(Stream &serial, uint8_t modbusAddress) {
    legacy_deviceAddress = modbusAddress;
    legacy_cfg.inverter_addr = modbusAddress;
    vpc_legacy_node.begin(modbusAddress, serial);
}

bool VPC_readStatus() {
    VPCTelemetry telem;
    bool ok = VPC_readTelemetry(vpc_legacy_node, legacy_cfg, telem);
    if (ok) {
        Serial.println("VPC Status odczytany:");
        VPC_debugStatus();
    } else {
        Serial.println("VPC Odczyt statusu: Błąd");
    }
    return ok;
}

bool VPC_start() {
    // Start command: set bit in control word (implementation depends on VPC spec)
    // For now, write to operation register with start bit
    return VPC_writeControlWord(vpc_legacy_node, legacy_cfg, 0x0001);
}

bool VPC_stop() {
    // Stop command: clear run bit
    return VPC_writeControlWord(vpc_legacy_node, legacy_cfg, 0x0000);
}

bool VPC_setFrequency(float freq) {
    uint16_t scaledFreq = (uint16_t)(freq * (float)legacy_cfg.freq_div);
    return VPC_writeSetFrequency(vpc_legacy_node, legacy_cfg, scaledFreq);
}

bool VPC_clearFault() {
    return VPC_clearFault(vpc_legacy_node, legacy_cfg);
}

void VPC_debugStatus() {
    Serial.print("VPC Status: ");
    Serial.println(vpc_legacy_node.getResponseBuffer(0));
}