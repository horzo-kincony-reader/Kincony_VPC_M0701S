"""
VPC-M0701S Inverter Reader
A Python library for communicating with Kincony VPC-M0701S Chinese frequency inverters via MODBUS RTU.
"""

__version__ = "0.1.0"
__author__ = "Kincony VPC-M0701S Reader Project"
__license__ = "MIT"

from .vpc_m0701s_reader import VPC_M0701S_Reader

__all__ = ["VPC_M0701S_Reader"]
