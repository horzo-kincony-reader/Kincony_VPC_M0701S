/*
 * Unit Tests for VPC M0701S Controller
 * 
 * This file contains unit tests for the VPC M0701S-specific features.
 * Tests can be run on Arduino or using a compatible testing framework.
 */

#include <Arduino.h>
#include "modbus_registers.h"

// Test counter
int testsRun = 0;
int testsPassed = 0;
int testsFailed = 0;

// Test helper macros
#define TEST_ASSERT(condition, message) \
  testsRun++; \
  if (condition) { \
    testsPassed++; \
    Serial.print("[PASS] "); \
    Serial.println(message); \
  } else { \
    testsFailed++; \
    Serial.print("[FAIL] "); \
    Serial.println(message); \
  }

#define TEST_ASSERT_EQUAL(expected, actual, message) \
  testsRun++; \
  if ((expected) == (actual)) { \
    testsPassed++; \
    Serial.print("[PASS] "); \
    Serial.println(message); \
  } else { \
    testsFailed++; \
    Serial.print("[FAIL] "); \
    Serial.print(message); \
    Serial.print(" - Expected: "); \
    Serial.print(expected); \
    Serial.print(", Got: "); \
    Serial.println(actual); \
  }

#define TEST_ASSERT_FLOAT_EQUAL(expected, actual, tolerance, message) \
  testsRun++; \
  if (abs((expected) - (actual)) <= (tolerance)) { \
    testsPassed++; \
    Serial.print("[PASS] "); \
    Serial.println(message); \
  } else { \
    testsFailed++; \
    Serial.print("[FAIL] "); \
    Serial.print(message); \
    Serial.print(" - Expected: "); \
    Serial.print(expected); \
    Serial.print(", Got: "); \
    Serial.println(actual); \
  }

// Test functions
void testRegisterConversions() {
  Serial.println("\n=== Testing Register Conversions ===");
  
  // Test frequency conversion
  float freq = 50.0;
  uint16_t freqReg = FREQ_TO_REG(freq);
  float freqBack = REG_TO_FREQ(freqReg);
  TEST_ASSERT_FLOAT_EQUAL(freq, freqBack, 0.01, "Frequency conversion (50.0 Hz)");
  
  freq = 60.5;
  freqReg = FREQ_TO_REG(freq);
  freqBack = REG_TO_FREQ(freqReg);
  TEST_ASSERT_FLOAT_EQUAL(freq, freqBack, 0.01, "Frequency conversion (60.5 Hz)");
  
  // Test current conversion
  float current = 15.5;
  uint16_t currentReg = CURRENT_TO_REG(current);
  float currentBack = REG_TO_CURRENT(currentReg);
  TEST_ASSERT_FLOAT_EQUAL(current, currentBack, 0.1, "Current conversion (15.5 A)");
  
  // Test temperature conversion
  float temp = 45.3;
  uint16_t tempReg = TEMP_TO_REG(temp);
  float tempBack = REG_TO_TEMP(tempReg);
  TEST_ASSERT_FLOAT_EQUAL(temp, tempBack, 0.1, "Temperature conversion (45.3°C)");
  
  // Test power conversion
  float power = 7.5;
  uint16_t powerReg = POWER_TO_REG(power);
  float powerBack = REG_TO_POWER(powerReg);
  TEST_ASSERT_FLOAT_EQUAL(power, powerBack, 0.1, "Power conversion (7.5 kW)");
}

void testCommandCodes() {
  Serial.println("\n=== Testing Command Codes ===");
  
  TEST_ASSERT_EQUAL(0x0000, CMD_STOP, "Stop command code");
  TEST_ASSERT_EQUAL(0x0001, CMD_START_FORWARD, "Start forward command code");
  TEST_ASSERT_EQUAL(0x0002, CMD_START_REVERSE, "Start reverse command code");
  TEST_ASSERT_EQUAL(0x0007, CMD_RESET_FAULT, "Reset fault command code");
  TEST_ASSERT_EQUAL(0x0008, CMD_EMERGENCY_STOP, "Emergency stop command code");
}

void testStatusBits() {
  Serial.println("\n=== Testing Status Bits ===");
  
  uint16_t status = STATUS_READY | STATUS_RUNNING | STATUS_FORWARD;
  
  TEST_ASSERT(status & STATUS_READY, "Status ready bit");
  TEST_ASSERT(status & STATUS_RUNNING, "Status running bit");
  TEST_ASSERT(status & STATUS_FORWARD, "Status forward bit");
  TEST_ASSERT(!(status & STATUS_REVERSE), "Status reverse bit (not set)");
  TEST_ASSERT(!(status & STATUS_FAULT), "Status fault bit (not set)");
}

void testFaultCodes() {
  Serial.println("\n=== Testing Fault Codes ===");
  
  TEST_ASSERT_EQUAL(0x0000, FAULT_NONE, "No fault code");
  TEST_ASSERT_EQUAL(0x0001, FAULT_OVERCURRENT, "Overcurrent fault code");
  TEST_ASSERT_EQUAL(0x0002, FAULT_OVERVOLTAGE, "Overvoltage fault code");
  TEST_ASSERT_EQUAL(0x0003, FAULT_UNDERVOLTAGE, "Undervoltage fault code");
  TEST_ASSERT_EQUAL(0x0004, FAULT_OVERLOAD, "Overload fault code");
  TEST_ASSERT_EQUAL(0x0005, FAULT_OVERTEMP, "Overtemperature fault code");
}

void testRegisterAddresses() {
  Serial.println("\n=== Testing Register Addresses ===");
  
  // Test holding registers
  TEST_ASSERT_EQUAL(0x2000, REG_CONTROL_COMMAND, "Control command register");
  TEST_ASSERT_EQUAL(0x2001, REG_FREQUENCY_SETPOINT, "Frequency setpoint register");
  TEST_ASSERT_EQUAL(0x2002, REG_ACCELERATION_TIME, "Acceleration time register");
  TEST_ASSERT_EQUAL(0x2003, REG_DECELERATION_TIME, "Deceleration time register");
  
  // Test input registers
  TEST_ASSERT_EQUAL(0x3000, REG_RUNNING_STATUS, "Running status register");
  TEST_ASSERT_EQUAL(0x3001, REG_OUTPUT_FREQUENCY, "Output frequency register");
  TEST_ASSERT_EQUAL(0x3002, REG_OUTPUT_CURRENT, "Output current register");
  TEST_ASSERT_EQUAL(0x3003, REG_OUTPUT_VOLTAGE, "Output voltage register");
  TEST_ASSERT_EQUAL(0x3008, REG_INVERTER_TEMP, "Inverter temperature register");
  TEST_ASSERT_EQUAL(0x3009, REG_FAULT_CODE, "Fault code register");
}

void testFrequencyRanges() {
  Serial.println("\n=== Testing Frequency Ranges ===");
  
  // Test typical frequency values
  float testFreqs[] = {0.0, 25.0, 50.0, 60.0, 100.0, 200.0, 400.0};
  int numTests = sizeof(testFreqs) / sizeof(testFreqs[0]);
  
  for (int i = 0; i < numTests; i++) {
    uint16_t reg = FREQ_TO_REG(testFreqs[i]);
    float back = REG_TO_FREQ(reg);
    
    char msg[50];
    sprintf(msg, "Frequency range test %.1f Hz", testFreqs[i]);
    TEST_ASSERT_FLOAT_EQUAL(testFreqs[i], back, 0.01, msg);
  }
}

void testEdgeCases() {
  Serial.println("\n=== Testing Edge Cases ===");
  
  // Zero values
  TEST_ASSERT_EQUAL(0, FREQ_TO_REG(0.0), "Zero frequency to register");
  TEST_ASSERT_FLOAT_EQUAL(0.0, REG_TO_FREQ(0), 0.01, "Zero register to frequency");
  
  // Maximum typical values
  uint16_t maxFreqReg = FREQ_TO_REG(400.0);
  TEST_ASSERT(maxFreqReg == 40000, "Maximum frequency (400 Hz) conversion");
  
  // Negative values (should be handled gracefully)
  // Note: In production, negative values should be validated before conversion
  
  // Test status bit combinations
  uint16_t allBits = STATUS_READY | STATUS_RUNNING | STATUS_FORWARD | 
                     STATUS_AT_FREQUENCY;
  TEST_ASSERT(allBits & STATUS_READY, "Combined status bits - Ready");
  TEST_ASSERT(allBits & STATUS_RUNNING, "Combined status bits - Running");
  TEST_ASSERT(allBits & STATUS_FORWARD, "Combined status bits - Forward");
  TEST_ASSERT(allBits & STATUS_AT_FREQUENCY, "Combined status bits - At Frequency");
}

void testMultiInverterSupport() {
  Serial.println("\n=== Testing Multi-Inverter Support ===");
  
  // Test that MAX_INVERTERS is reasonable
  TEST_ASSERT(MAX_INVERTERS >= 1 && MAX_INVERTERS <= 10, 
              "MAX_INVERTERS in valid range (1-10)");
  
  // Test slave ID generation for multiple inverters
  for (int i = 0; i < MAX_INVERTERS; i++) {
    uint8_t slaveId = i + 1;
    TEST_ASSERT(slaveId >= 1 && slaveId <= 247, 
                "Slave ID in valid Modbus range");
  }
}

void runAllTests() {
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("VPC M0701S Unit Tests");
  Serial.println("========================================");
  
  testsRun = 0;
  testsPassed = 0;
  testsFailed = 0;
  
  testRegisterConversions();
  testCommandCodes();
  testStatusBits();
  testFaultCodes();
  testRegisterAddresses();
  testFrequencyRanges();
  testEdgeCases();
  testMultiInverterSupport();
  
  Serial.println("\n========================================");
  Serial.println("Test Summary");
  Serial.println("========================================");
  Serial.print("Total Tests: ");
  Serial.println(testsRun);
  Serial.print("Passed: ");
  Serial.println(testsPassed);
  Serial.print("Failed: ");
  Serial.println(testsFailed);
  
  if (testsFailed == 0) {
    Serial.println("\n✓ ALL TESTS PASSED!");
  } else {
    Serial.println("\n✗ SOME TESTS FAILED");
  }
  Serial.println("========================================\n");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  runAllTests();
}

void loop() {
  // Tests run once in setup
  delay(1000);
}
