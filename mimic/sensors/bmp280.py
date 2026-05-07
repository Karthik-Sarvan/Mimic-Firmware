import logging
import time
import math
from ..bridge import I2CSlave, SPISlave, MimicBridge

logger = logging.getLogger("Mimic")

class BMP280Simulator:
    """Simulates a BMP280 Temperature and Pressure sensor."""
    
    ID_REG = 0xD0
    RESET_REG = 0xE0
    STATUS_REG = 0xF3
    CTRL_MEAS_REG = 0xF4
    CONFIG_REG = 0xF5
    PRESS_MSB = 0xF7
    TEMP_MSB = 0xFA
    
    def __init__(self, bridge: MimicBridge, address: int = 0x76, protocol: str = "spi"):
        if protocol.lower() == "spi":
            self.bus = SPISlave(bridge, instance=1)
        else:
            self.bus = I2CSlave(bridge, instance=1)
            
        self.address = address
        self.protocol = protocol.lower()
        
    def _to_u16_le(self, val):
        return [val & 0xFF, (val >> 8) & 0xFF]

    def _to_s16_le(self, val):
        if val < 0:
            val = (1 << 16) + val
        return [val & 0xFF, (val >> 8) & 0xFF]

    def start(self):
        logger.info(f"Starting BMP280 Simulator over {self.protocol.upper()}...")
        
        if self.protocol == "spi":
            # SPI mode for BMP280 is usually Mode 0 (CPOL=0, CPHA=0)
            if not self.bus.init(speed=10000000, cpol=0, cpha=0):
                logger.error("Failed to initialize SPI Slave")
                return
        else:
            if not self.bus.init(self.address):
                logger.error("Failed to initialize I2C Slave")
                return

        # 1. SEED ID (0x58)
        self.bus.set_register(self.ID_REG, 0x58)

        # 2. SEED CALIBRATION (Bulk)
        calib = []
        calib.extend(self._to_u16_le(27500)) # T1
        calib.extend(self._to_s16_le(26435)) # T2
        calib.extend(self._to_s16_le(-1000)) # T3
        calib.extend(self._to_u16_le(36477)) # P1
        calib.extend(self._to_s16_le(-10685))# P2
        calib.extend(self._to_s16_le(3024))  # P3
        calib.extend(self._to_s16_le(2855))  # P4
        calib.extend(self._to_s16_le(140))   # P5
        calib.extend(self._to_s16_le(-7))    # P6
        calib.extend(self._to_s16_le(15500)) # P7
        calib.extend(self._to_s16_le(-14600))# P8
        calib.extend(self._to_s16_le(6000))  # P9
        self.bus.set_registers(0x88, calib)

        logger.info(f"BMP280 Simulation Ready on {self.protocol.upper()}")

        t = 0
        while True:
            t += 0.05
            temp_raw = 512000 + int(math.sin(t) * 10000)
            press_raw = 415000 + int(math.cos(t) * 5000)

            burst_data = [
                (press_raw >> 12) & 0xFF, (press_raw >> 4) & 0xFF, (press_raw << 4) & 0xFF,
                (temp_raw >> 12) & 0xFF, (temp_raw >> 4) & 0xFF, (temp_raw << 4) & 0xFF
            ]
            self.bus.set_registers(0xF7, burst_data)
            time.sleep(0.1)
