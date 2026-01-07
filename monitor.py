#!/usr/bin/env python3
"""
Simple monitoring script for VPC-M0701S inverter.
Continuously reads and displays inverter status.
"""

import logging
import time
import sys
from vpc_reader import VPC_M0701S_Reader


def main():
    # Configure logging
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(levelname)s - %(message)s'
    )
    
    logger = logging.getLogger(__name__)
    
    try:
        with VPC_M0701S_Reader(config_path="config.yml") as reader:
            logger.info("Starting inverter monitoring...")
            logger.info("Press Ctrl+C to stop")
            
            while True:
                try:
                    # Clear screen (works on Unix-like systems)
                    print("\033[2J\033[H", end="")
                    
                    # Read status
                    status = reader.read_status()
                    
                    if status:
                        print("=" * 50)
                        print("VPC-M0701S Inverter Status")
                        print("=" * 50)
                        print(f"Output Frequency: {status['output_frequency']:.2f} Hz")
                        print(f"Output Current:   {status['output_current']:.2f} A")
                        print(f"Output Voltage:   {status['output_voltage']:.1f} V")
                        print(f"DC Bus Voltage:   {status['dc_bus_voltage']:.1f} V")
                        print("=" * 50)
                        print(f"Last update: {time.strftime('%Y-%m-%d %H:%M:%S')}")
                    else:
                        print("Error reading status")
                    
                    # Wait before next read
                    time.sleep(1)
                    
                except KeyboardInterrupt:
                    logger.info("\nMonitoring stopped by user")
                    break
                    
    except FileNotFoundError:
        logger.error("Configuration file not found. Please create config.yml")
        sys.exit(1)
    except Exception as e:
        logger.error(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
