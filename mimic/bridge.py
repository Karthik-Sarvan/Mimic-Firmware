import serial
import serial.tools.list_ports
import time
import logging
from typing import List, Optional, Union

logger = logging.getLogger("Mimic")

class MimicBridge:
    """Core communication class with Auto-Discovery."""
    
    def __init__(self, port: Optional[str] = None, baud: int = 115200, timeout: float = 1.0):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser = None

    @staticmethod
    def discover_ports() -> List[str]:
        """List all potential serial ports."""
        return [p.device for p in serial.tools.list_ports.comports()]

    def connect(self) -> bool:
        """Connect to the first available Mimic board or a specific port."""
        ports_to_try = [self.port] if self.port else self.discover_ports()
        
        for p in ports_to_try:
            try:
                logger.info(f"Checking {p}...")
                self.ser = serial.Serial(p, self.baud, timeout=2.0)
                self.ser.reset_input_buffer()
                
                # Send a test character to wake up the buffer
                self.ser.write(b"\r\n")
                time.sleep(0.2)
                self.ser.reset_input_buffer()
                
                # Check for Mimic version
                self.send_command("VERSION")
                time.sleep(0.5)  # Give the STM32 more time to respond
                response = self.read_response()
                
                if any("MIMIC" in line.upper() for line in response):
                    self.port = p
                    logger.info(f"Successfully auto-detected Mimic on {p}")
                    return True
                
                self.ser.close()
            except Exception:
                continue
                
        logger.error("No Mimic board found. Check your USB connection.")
        return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_command(self, cmd: str):
        if self.ser and self.ser.is_open:
            self.ser.write(f"{cmd}\r\n".encode('utf-8'))

    def read_response(self) -> List[str]:
        lines = []
        if not self.ser or not self.ser.is_open:
            return lines
        
        time.sleep(0.1)
        while self.ser.in_waiting:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if line and line != ">":
                lines.append(line)
        return lines

    def execute(self, cmd: str) -> List[str]:
        self.send_command(cmd)
        return self.read_response()

class I2CSlave:
    """I2C Slave helper."""
    def __init__(self, bridge: MimicBridge, instance: int = 1):
        self.bridge = bridge
        self.instance = instance

    def init(self, address: Union[int, str]) -> bool:
        addr_str = f"0x{address:02X}" if isinstance(address, int) else address
        resp = self.bridge.execute(f"I2C_INIT {self.instance} SLAVE {addr_str}")
        return any("OK" in r for r in resp)

    def wait_for_write(self, length: int = 1) -> Optional[List[int]]:
        """Wait for the Master to write to us (Interrupt-driven support)."""
        # Start the listen mode if not already active
        self.bridge.send_command(f"I2C_READ {self.instance} 0x00 {length}")
        
        timeout = 1.0 # Short polling timeout
        start_time = time.time()
        while time.time() - start_time < timeout:
            resp = self.bridge.read_response()
            for r in resp:
                if "I2C_EVENT: WRITE" in r:
                    try:
                        data_str = r.split("WRITE")[1].strip()
                        return [int(data_str, 16)]
                    except (IndexError, ValueError):
                        pass
            time.sleep(0.01)
        return None

    def set_register(self, reg: int, val: int) -> bool:
        """Set a single register value in the STM32 shadow map."""
        resp = self.bridge.execute(f"I2C_REG_SET 0x{reg:02X} 0x{val:02X}")
        return any("OK" in r for r in resp)

    def set_registers(self, start_reg: int, data: List[int]) -> bool:
        """Set a block of registers in the STM32 shadow map."""
        data_str = "".join([f"{x:02X}" for x in data])
        resp = self.bridge.execute(f"I2C_REG_DATA 0x{start_reg:02X} {data_str}")
        return any("OK" in r for r in resp)

    def respond(self, data: List[int]) -> bool:
        """Pre-load data for the next Master READ request. (Compatibility wrapper)"""
        # Default to dumping at 0x3B (standard MPU6050 data start) if not otherwise specified
        return self.set_registers(0x3B, data)

class SPISlave:
    """SPI Slave helper."""
    def __init__(self, bridge: MimicBridge, instance: int = 1):
        self.bridge = bridge
        self.instance = instance

    def init(self, speed: int = 1000000, cpol: int = 0, cpha: int = 0) -> bool:
        """Initialize Mimic as an SPI Slave."""
        cmd = f"SPI_INIT {self.instance} SLAVE {speed} {cpol} {cpha}"
        resp = self.bridge.execute(cmd)
        return any("OK" in r for r in resp)

    def set_register(self, reg: int, val: int) -> bool:
        """Set a single register in the shared map."""
        # SPI and I2C share the same underlying register map in firmware
        resp = self.bridge.execute(f"I2C_REG_SET 0x{reg:02X} 0x{val:02X}")
        return any("OK" in r for r in resp)

    def set_registers(self, start_reg: int, data: List[int]) -> bool:
        """Set a block of registers in the shared map."""
        data_str = "".join([f"{x:02X}" for x in data])
        resp = self.bridge.execute(f"I2C_REG_DATA 0x{start_reg:02X} {data_str}")
        return any("OK" in r for r in resp)
