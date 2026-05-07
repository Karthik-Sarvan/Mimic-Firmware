import time
import logging
from ..bridge import MimicBridge

logger = logging.getLogger("Mimic")

class GPSSimulator:
    """Simulates a NEO-6M GPS Module sending NMEA sentences."""
    
    def __init__(self, bridge: MimicBridge, instance: int = 6):
        self.bridge = bridge
        self.instance = instance
        self.lat = 12.9716  # Starting at Bangalore (or any location!)
        self.lon = 77.5946
        
    def start(self, baud: int = 9600):
        logger.info(f"Starting GPS Simulation on UART{self.instance} at {baud} baud...")
        
        # Initialize UART on STM32
        self.bridge.execute(f"UART_INIT {self.instance} {baud}")
        
        t = 0
        while True:
            try:
                # Simulate a slow walk (increment lat/lon slightly)
                self.lat += 0.0001
                self.lon += 0.0001
                
                # Format into NMEA GPRMC string (Recommended Minimum Navigation Information)
                # Example: $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
                nmea_time = time.strftime("%H%M%S", time.gmtime())
                nmea_date = time.strftime("%d%m%y", time.gmtime())
                
                # Convert decimal lat/lon to NMEA degrees/minutes
                lat_deg = int(self.lat)
                lat_min = (self.lat - lat_deg) * 60
                lon_deg = int(self.lon)
                lon_min = (self.lon - lon_deg) * 60
                
                # Build the sentence
                sentence = f"GPRMC,{nmea_time},A,{lat_deg:02d}{lat_min:07.4f},N,{lon_deg:03d}{lon_min:07.4f},E,0.0,0.0,{nmea_date},,,A"
                
                # Calculate Checksum
                checksum = 0
                for char in sentence:
                    checksum ^= ord(char)
                
                full_sentence = f"${sentence}*{checksum:02X}"
                
                # Send to STM32 UART
                # We use \n for line ending
                self.bridge.execute(f'UART_SEND {self.instance} "{full_sentence}\\r\\n"')
                
                logger.info(f"GPS Sent: {full_sentence}")
                time.sleep(1.0) # GPS typically updates at 1Hz
                
            except KeyboardInterrupt:
                break

    def _to_nmea(self, val, is_lat):
        # Helper for lat/lon conversion if needed
        pass
