#ifndef VPC_MODBUS_H
#define VPC_MODBUS_H

#include <Arduino.h>
#include <ModbusMaster.h> // Biblioteka obsługująca Modbus RTU

// Stałe dla adresów rejestrów Modbus
#define MODBUS_SET_FREQUENCY       0x9C42  // Adres częstotliwości
#define MODBUS_COMMAND             0x9C40  // Adres start/stop
#define MODBUS_RUNNING_STATUS      40180   // Status: run/stop
#define MODBUS_FAULT_STATUS        40189   // Status alarmów
#define MODBUS_CLEAR_FAULT         40198   // Kasowanie błędów

// Inicjalizacja Modbus RTU dla konkretnego falownika
void VPC_init(Stream &serial, uint8_t modbusAddress);

// API rejestrów (funkcje sterowania falownikiem)
bool VPC_readStatus();               // Odczyt statusów pracy
bool VPC_setFrequency(float freq);   // Zmiana częstotliwości
bool VPC_start();                    // Start falownika
bool VPC_stop();                     // Stop falownika
bool VPC_clearFault();               // Kasowanie błędów

// Funkcje pomocnicze (opcjonalne logi / debug):
void VPC_debugStatus();

#endif // VPC_MODBUS_H