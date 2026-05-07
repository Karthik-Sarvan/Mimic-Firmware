import logging
from typing import List
from ..bridge import I2CSlave, MimicBridge

logger = logging.getLogger("Mimic")

class MPU6050Simulator:
    """Simulates an MPU6050 Accelerometer/Gyroscope."""
    
    WHO_AM_I_REG = 0x75
    PWR_MGMT_1_REG = 0x6B
    ACCEL_XOUT_H = 0x3B
    GYRO_XOUT_H = 0x43
    TEMP_OUT_H = 0x41
    
    def __init__(self, bridge: MimicBridge, address: int = 0x68):
        self.i2c = I2CSlave(bridge)
        self.address = address
        self.registers = {
            self.WHO_AM_I_REG: [0x68],
            self.PWR_MGMT_1_REG: [0x40],
            0x1A: [0x00],
            0x1B: [0x00],
            0x1C: [0x00],
        }
        self.accel_data = [0, 0, 16384]
        self.gyro_data = [0, 0, 0]
        self.temp_raw = 3650

    def start(self):
        logger.info(f"Starting MPU6050 Simulator at address 0x{self.address:02X}...")
        if not self.i2c.init(self.address):
            logger.error("Failed to initialize I2C Slave")
            return

        # Seed the shadow register map so the STM32 can answer handshakes instantly
        logger.info("Seeding MPU6050 register map...")
        self.i2c.set_register(self.WHO_AM_I_REG, 0x68)
        self.i2c.set_register(self.PWR_MGMT_1_REG, 0x00) # Wake up!

        import time
        import math
        
        logger.info("Initializing MPU6050 simulation...")
        
        t = 0
        while True:
            try:
                # Generate some dynamic fake accelerometer data
                t += 0.1
                self.accel_data[0] = int(math.sin(t) * 8192)
                self.accel_data[1] = int(math.cos(t) * 8192)
                self.accel_data[2] = 16384 # 1g Z-axis
                
                # We simply push the ACCEL, GYRO and TEMP data as a 14-byte continuous block
                # This matches what Adafruit_MPU6050 normally reads in getEvent()
                data_to_send = []
                data_to_send.extend(self._to_16bit_list(self.accel_data))
                data_to_send.extend([(self.temp_raw >> 8) & 0xFF, self.temp_raw & 0xFF])
                data_to_send.extend(self._to_16bit_list(self.gyro_data))
                
                self.i2c.respond(data_to_send)
                time.sleep(0.1) # 10Hz tick - Back to full speed!
            except KeyboardInterrupt:
                break

    def _get_reg_data(self, reg: int) -> List[int]:
        if reg == self.WHO_AM_I_REG:
            return [0x68]
        elif reg == self.ACCEL_XOUT_H:
            return self._to_16bit_list(self.accel_data)
        elif reg == self.GYRO_XOUT_H:
            return self._to_16bit_list(self.gyro_data)
        elif reg == self.TEMP_OUT_H:
            return [(self.temp_raw >> 8) & 0xFF, self.temp_raw & 0xFF]
        return self.registers.get(reg, [0x00])

    def _to_16bit_list(self, values: List[int]) -> List[int]:
        res = []
        for v in values:
            v &= 0xFFFF
            res.append((v >> 8) & 0xFF)
            res.append(v & 0xFF)
        return res
