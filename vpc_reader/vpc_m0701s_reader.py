"""
VPC-M0701S Reader Class
Handles communication with the Kincony VPC-M0701S inverter via MODBUS RTU protocol.
"""

import logging
from typing import Optional, Dict, Any
import yaml
from pymodbus.client import ModbusSerialClient
from pymodbus.exceptions import ModbusException


class VPC_M0701S_Reader:
    """
    Reader class for Kincony VPC-M0701S frequency inverter.
    
    This class provides methods to read status registers and control the inverter
    via MODBUS RTU protocol over RS485 serial connection.
    """
    
    def __init__(self, config_path: str = "config.yml"):
        """
        Initialize the VPC-M0701S reader.
        
        Args:
            config_path: Path to the YAML configuration file
        """
        self.logger = logging.getLogger(__name__)
        self.config = self._load_config(config_path)
        self.client: Optional[ModbusSerialClient] = None
        self.slave_id = self.config['inverter']['slave_id']
        self.registers = self.config['inverter']['registers']
        
    def _load_config(self, config_path: str) -> Dict[str, Any]:
        """Load configuration from YAML file."""
        try:
            with open(config_path, 'r') as f:
                return yaml.safe_load(f)
        except FileNotFoundError:
            self.logger.error(f"Configuration file not found: {config_path}")
            raise
        except yaml.YAMLError as e:
            self.logger.error(f"Error parsing configuration file: {e}")
            raise
    
    def connect(self) -> bool:
        """
        Establish connection to the inverter.
        
        Returns:
            True if connection successful, False otherwise
        """
        try:
            inv_config = self.config['inverter']
            self.client = ModbusSerialClient(
                port=inv_config['port'],
                baudrate=inv_config['baudrate'],
                bytesize=inv_config['bytesize'],
                parity=inv_config['parity'],
                stopbits=inv_config['stopbits'],
                timeout=inv_config['timeout']
            )
            
            if self.client.connect():
                self.logger.info(f"Connected to inverter on {inv_config['port']}")
                return True
            else:
                self.logger.error("Failed to connect to inverter")
                return False
        except Exception as e:
            self.logger.error(f"Connection error: {e}")
            return False
    
    def disconnect(self) -> None:
        """Close connection to the inverter."""
        if self.client:
            self.client.close()
            self.logger.info("Disconnected from inverter")
    
    def read_register(self, register_address: int, count: int = 1) -> Optional[list]:
        """
        Read holding register(s) from the inverter.
        
        Args:
            register_address: Starting register address
            count: Number of registers to read
            
        Returns:
            List of register values, or None if error
        """
        if not self.client or not self.client.connected:
            self.logger.error("Not connected to inverter")
            return None
        
        try:
            result = self.client.read_holding_registers(
                address=register_address,
                count=count,
                slave=self.slave_id
            )
            
            if result.isError():
                self.logger.error(f"Error reading register {register_address}: {result}")
                return None
            
            return result.registers
        except ModbusException as e:
            self.logger.error(f"MODBUS error reading register {register_address}: {e}")
            return None
    
    def write_register(self, register_address: int, value: int) -> bool:
        """
        Write to a holding register.
        
        Args:
            register_address: Register address
            value: Value to write
            
        Returns:
            True if successful, False otherwise
        """
        if not self.client or not self.client.connected:
            self.logger.error("Not connected to inverter")
            return False
        
        try:
            result = self.client.write_register(
                address=register_address,
                value=value,
                slave=self.slave_id
            )
            
            if result.isError():
                self.logger.error(f"Error writing register {register_address}: {result}")
                return False
            
            self.logger.debug(f"Wrote {value} to register {register_address}")
            return True
        except ModbusException as e:
            self.logger.error(f"MODBUS error writing register {register_address}: {e}")
            return False
    
    def read_output_frequency(self) -> Optional[float]:
        """
        Read current output frequency.
        
        Returns:
            Output frequency in Hz, or None if error
        """
        registers = self.read_register(self.registers['output_frequency'])
        if registers:
            # Convert register value to Hz (scaling may need adjustment based on actual device)
            return registers[0] / 100.0
        return None
    
    def read_output_current(self) -> Optional[float]:
        """
        Read current output current.
        
        Returns:
            Output current in Amperes, or None if error
        """
        registers = self.read_register(self.registers['output_current'])
        if registers:
            # Convert register value to Amperes (scaling may need adjustment)
            return registers[0] / 100.0
        return None
    
    def read_output_voltage(self) -> Optional[float]:
        """
        Read current output voltage.
        
        Returns:
            Output voltage in Volts, or None if error
        """
        registers = self.read_register(self.registers['output_voltage'])
        if registers:
            # Convert register value to Volts (scaling may need adjustment)
            return registers[0] / 10.0
        return None
    
    def read_dc_bus_voltage(self) -> Optional[float]:
        """
        Read DC bus voltage.
        
        Returns:
            DC bus voltage in Volts, or None if error
        """
        registers = self.read_register(self.registers['dc_bus_voltage'])
        if registers:
            return registers[0] / 10.0
        return None
    
    def read_status(self) -> Optional[Dict[str, Any]]:
        """
        Read all status registers.
        
        Returns:
            Dictionary with all status values, or None if error
        """
        return {
            'output_frequency': self.read_output_frequency(),
            'output_current': self.read_output_current(),
            'output_voltage': self.read_output_voltage(),
            'dc_bus_voltage': self.read_dc_bus_voltage(),
        }
    
    def set_frequency(self, frequency: float) -> bool:
        """
        Set target frequency.
        
        Args:
            frequency: Target frequency in Hz
            
        Returns:
            True if successful, False otherwise
        """
        # Convert Hz to register value (scaling may need adjustment)
        reg_value = int(frequency * 100)
        return self.write_register(self.registers['frequency_setpoint'], reg_value)
    
    def start(self) -> bool:
        """
        Start the inverter.
        
        Returns:
            True if successful, False otherwise
        """
        return self.write_register(self.registers['run_command'], 1)
    
    def stop(self) -> bool:
        """
        Stop the inverter.
        
        Returns:
            True if successful, False otherwise
        """
        return self.write_register(self.registers['run_command'], 0)
    
    def __enter__(self):
        """Context manager entry."""
        if not self.connect():
            raise ConnectionError("Failed to connect to inverter")
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.disconnect()
