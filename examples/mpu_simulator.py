#!/usr/bin/env python3
from mimic import MimicBridge, MPU6050Simulator
import sys

def main():
    # Configuration
    PORT = "/dev/ttyUSB0"  # Change this to your port
    BAUD = 115200
    
    # Initialize Bridge
    bridge = MimicBridge(PORT, BAUD)
    
    if not bridge.connect():
        print(f"Error: Could not connect to Mimic on {PORT}")
        sys.exit(1)
        
    try:
        # Initialize MPU6050 Simulator
        # This will set the STM32 I2C1 to Slave Mode at address 0x68
        sim = MPU6050Simulator(bridge, address=0x68)
        
        # Add some custom fake data if you want
        sim.accel_data = [1500, -500, 16000] # Random X, Y and roughly 1g on Z
        sim.gyro_data = [10, 0, -5]
        
        # Start the simulation loop
        print("\nMIMIC MPU6050 SIMULATOR")
        print("-----------------------")
        print("STM32 is now acting as a Slave I2C device.")
        print("Wiring: Connect Master SDA to PB9, SCL to PB8, and GND to GND.")
        print("Press Ctrl+C to stop.")
        
        sim.start()
        
    except Exception as e:
        print(f"Simulator Error: {e}")
    finally:
        bridge.disconnect()
        print("Connection closed.")

if __name__ == "__main__":
    main()
