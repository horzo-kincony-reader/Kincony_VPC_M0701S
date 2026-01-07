#!/usr/bin/env python3
"""
Connection test script for VPC-M0701S inverter.
This script tests basic connectivity without issuing any control commands.
"""

import logging
import sys
from vpc_reader import VPC_M0701S_Reader


def test_connection():
    """Test connection to the inverter."""
    logging.basicConfig(
        level=logging.DEBUG,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    logger = logging.getLogger(__name__)
    
    logger.info("=" * 60)
    logger.info("VPC-M0701S Connection Test")
    logger.info("=" * 60)
    
    try:
        # Load configuration
        logger.info("Loading configuration from config.yml...")
        reader = VPC_M0701S_Reader(config_path="config.yml")
        logger.info("✓ Configuration loaded successfully")
        logger.info(f"  Port: {reader.config['inverter']['port']}")
        logger.info(f"  Baud rate: {reader.config['inverter']['baudrate']}")
        logger.info(f"  Slave ID: {reader.slave_id}")
        
        # Test connection
        logger.info("\nAttempting to connect to inverter...")
        if reader.connect():
            logger.info("✓ Connection established successfully")
            
            # Try a simple read operation
            logger.info("\nTesting read operation...")
            try:
                freq = reader.read_output_frequency()
                if freq is not None:
                    logger.info(f"✓ Read operation successful")
                    logger.info(f"  Output frequency: {freq} Hz")
                else:
                    logger.warning("✗ Read operation returned None")
                    logger.warning("  This may indicate incorrect register addresses")
            except Exception as e:
                logger.error(f"✗ Read operation failed: {e}")
            
            # Disconnect
            reader.disconnect()
            logger.info("\n✓ Disconnected from inverter")
            
            logger.info("\n" + "=" * 60)
            logger.info("CONNECTION TEST PASSED")
            logger.info("=" * 60)
            logger.info("\nYour setup is working correctly!")
            logger.info("You can now use example_usage.py or monitor.py")
            return True
            
        else:
            logger.error("✗ Failed to establish connection")
            logger.error("\nPossible causes:")
            logger.error("  - Incorrect port name in config.yml")
            logger.error("  - RS485 adapter not connected")
            logger.error("  - Inverter not powered on")
            logger.error("  - Incorrect wiring (check A+ and B- terminals)")
            logger.error("  - Permission issues (try: sudo usermod -a -G dialout $USER)")
            return False
            
    except FileNotFoundError:
        logger.error("✗ Configuration file 'config.yml' not found")
        logger.error("  Please create config.yml based on the template")
        return False
    except Exception as e:
        logger.error(f"✗ Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return False


if __name__ == "__main__":
    success = test_connection()
    sys.exit(0 if success else 1)
