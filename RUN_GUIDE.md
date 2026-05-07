# Setup and Execution Guide

This guide details the process for deploying the Mimic firmware to your hardware and configuring the Python bridge for sensor emulation.

## 1. Hardware Preparation

Before proceeding with the software setup, ensure your hardware is correctly interfaced:

- **Board**: STM32F411CEU6 (BlackPill).
- **Interface**: Connect a USB-to-TTL adapter to **PA9 (TX)** and **PA10 (RX)** of the STM32.
- **Power**: Ensure the board is powered via USB or an external 3.3V source.

---

## 2. Firmware Deployment

The Mimic firmware handles low-level protocol logic. You must flash the pre-compiled binary or build it from source.

### Flashing the Binary
1. Connect your board via an ST-Link or similar SWD programmer.
2. Use a tool like **STM32CubeProgrammer** or **OpenOCD** to flash the `firmware/build/Mimic.bin` file to the starting address `0x08000000`.

### Building from Source (Optional)
If you wish to modify the firmware, ensure you have the `arm-none-eabi-gcc` toolchain installed:
```bash
cd firmware
make
```

---

## 3. Python Bridge Setup

The Python bridge allows your computer to communicate with the STM32 over the serial interface.

### Installation
Clone the repository and install the package in editable mode to ensure all dependencies are resolved:
```bash
git clone https://github.com/Karthik-Sarvan/Mimic.git
cd Mimic
pip install -e .
```

### Verification
To verify the setup, launch the interactive shell:
```bash
mimic
```
If the connection is successful, you will see a status message indicating the bridge is active.

---

## 4. Running Simulations

Mimic provides a built-in CLI for immediate sensor mocking.

### CLI Emulation
To act as an I2C sensor (e.g., MPU6050) on the hardware bus:
```bash
mimic-sim mpu6050
```

### Scripted Automation
For custom testing workflows, import the bridge into your Python environment:
```python
from mimic import MimicBridge

# Establish connection
bridge = MimicBridge()
if bridge.connect():
    # Execute hardware commands
    bridge.execute("STATUS")
```

---

**Aegion Dynamics**
