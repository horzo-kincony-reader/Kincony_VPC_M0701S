#!/usr/bin/env python3
"""
Setup script for VPC-M0701S Inverter Reader.
"""

from setuptools import setup, find_packages
import os

# Read README for long description
def read_readme():
    try:
        with open("README.md", "r", encoding="utf-8") as f:
            return f.read()
    except FileNotFoundError:
        return "VPC-M0701S Inverter Reader - Python library for Kincony VPC-M0701S frequency inverters"

# Read requirements
def read_requirements():
    try:
        with open("requirements.txt", "r", encoding="utf-8") as f:
            return [line.strip() for line in f if line.strip() and not line.startswith("#")]
    except FileNotFoundError:
        return ["pymodbus>=3.6.0", "pyserial>=3.5", "pyyaml>=6.0"]

setup(
    name="vpc-m0701s-reader",
    version="0.1.0",
    author="Kincony VPC-M0701S Reader Project",
    description="Python library for communicating with Kincony VPC-M0701S frequency inverters via MODBUS RTU",
    long_description=read_readme(),
    long_description_content_type="text/markdown",
    url="https://github.com/horzo-kincony-reader/Kincony_VPC_M0701S",
    packages=find_packages(),
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "Topic :: System :: Hardware :: Hardware Drivers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
    ],
    python_requires=">=3.7",
    install_requires=read_requirements(),
    include_package_data=True,
    keywords="modbus inverter vfd vpc-m0701s vfc-m0701s kincony rs485 automation",
    project_urls={
        "Bug Reports": "https://github.com/horzo-kincony-reader/Kincony_VPC_M0701S/issues",
        "Source": "https://github.com/horzo-kincony-reader/Kincony_VPC_M0701S",
    },
)
