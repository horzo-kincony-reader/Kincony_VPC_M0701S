#!/usr/bin/env python3
"""
Example script demonstrating how to use the VPC-M0701S Reader.
"""

import logging
import time
from vpc_reader import VPC_M0701S_Reader


def main():
    # Configure logging
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    logger = logging.getLogger(__name__)
    
    # Initialize reader with config file
    logger.info("Initializing VPC-M0701S Reader...")
    
    try:
        # Use context manager for automatic connection/disconnection
        with VPC_M0701S_Reader(config_path="config.yml") as reader:
            logger.info("Connected to inverter")
            
            # Read current status
            logger.info("Reading inverter status...")
            status = reader.read_status()
            
            if status:
                logger.info("Current Status:")
                logger.info(f"  Output Frequency: {status['output_frequency']} Hz")
                logger.info(f"  Output Current: {status['output_current']} A")
                logger.info(f"  Output Voltage: {status['output_voltage']} V")
                logger.info(f"  DC Bus Voltage: {status['dc_bus_voltage']} V")
            else:
                logger.error("Failed to read status")
            
            # Example: Set frequency and start inverter
            # Uncomment to actually control the inverter
            """
            logger.info("Setting frequency to 50 Hz...")
            if reader.set_frequency(50.0):
                logger.info("Frequency set successfully")
            else:
                logger.error("Failed to set frequency")
            
            logger.info("Starting inverter...")
            if reader.start():
                logger.info("Inverter started")
                
                # Monitor for a few seconds
                for i in range(5):
                    time.sleep(1)
                    freq = reader.read_output_frequency()
                    logger.info(f"Current frequency: {freq} Hz")
                
                # Stop the inverter
                logger.info("Stopping inverter...")
                if reader.stop():
                    logger.info("Inverter stopped")
            else:
                logger.error("Failed to start inverter")
            """
            
    except FileNotFoundError:
        logger.error("Configuration file not found. Please create config.yml")
    except Exception as e:
        logger.error(f"Error: {e}")


if __name__ == "__main__":
    main()
