#include "VPC_Modbus.h"

// ModbusMaster obiekt dla obsługi komunikacji (dołączana biblioteka Modbus)
ModbusMaster node;

// Adres falownika
static uint8_t deviceAddress = 1; // Factory default = 1

// Inicjalizacja Modbus RTU
void VPC_init(Stream &serial, uint8_t modbusAddress) {
    deviceAddress = modbusAddress;     // Adres falownika
    node.begin(deviceAddress, serial); // Inicjalizuj ModbusMaster
}

// Odczyt statusów falownika
bool VPC_readStatus() {
    uint8_t result;
    result = node.readHoldingRegisters(MODBUS_RUNNING_STATUS, 10); // 10 rejestrów od 40180
    if (result == node.ku8MBSuccess) {
        Serial.println("Status odczytany:");
        VPC_debugStatus();
        return true;
    } else {
        Serial.println("Odczyt statusu: Błąd");
        return false;
    }
}

// Start pracy
bool VPC_start() {
    uint8_t result = node.writeSingleRegister(MODBUS_COMMAND, 0x0001); // 1 = RUN
    return (result == node.ku8MBSuccess);
}

// Stop pracy
bool VPC_stop() {
    uint8_t result = node.writeSingleRegister(MODBUS_COMMAND, 0x0000); // 0 = STOP
    return (result == node.ku8MBSuccess);
}

// Ustawianie częstotliwości
bool VPC_setFrequency(float freq) {
    uint16_t scaledFreq = (uint16_t)(freq * 100); // np. 50.00 Hz -> 5000
    uint8_t result = node.writeSingleRegister(MODBUS_SET_FREQUENCY, scaledFreq);
    return (result == node.ku8MBSuccess);
}

// Kasowanie błędów
bool VPC_clearFault() {
    uint8_t result = node.writeSingleRegister(MODBUS_CLEAR_FAULT, 1); // Wartość > 0 = reset alarmu
    return (result == node.ku8MBSuccess);
}

// (Debug) Wydruk statusów w konsoli
void VPC_debugStatus() {
    Serial.print("Status pracy: ");
    Serial.println(node.getResponseBuffer(0)); // Przykład dla rejestrów 40180...
}