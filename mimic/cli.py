import sys
import argparse
import logging
import time

# Try to import readline for arrow history
try:
    import readline
except ImportError:
    pass 

from mimic import MimicBridge
from mimic.sensors.mpu6050 import MPU6050Simulator
from mimic.sensors.bmp280 import BMP280Simulator
from mimic.sensors.gps import GPSSimulator

# --- Gruvbox Color Theme (High-End 256-Color ANSI) ---
class Colors:
    PROMPT = '\033[38;5;109m'   # Gruvbox Aqua
    SUCCESS = '\033[38;5;142m'  # Gruvbox Green
    INFO = '\033[38;5;214m'     # Gruvbox Yellow/Orange
    ERROR = '\033[38;5;167m'    # Gruvbox Red
    TEXT = '\033[38;5;223m'     # Gruvbox Retro Cream
    BOLD = '\033[1m'
    END = '\033[0m'

def start_interactive_shell(bridge: MimicBridge):
    print(f"\n{Colors.PROMPT}{Colors.BOLD}{'='*55}{Colors.END}")
    print(f"{Colors.PROMPT}{Colors.BOLD}      MIMIC v1.1.0 - GRUVBOX HARDWARE TERMINAL{Colors.END}")
    print(f"{Colors.PROMPT}{Colors.BOLD}{'='*55}{Colors.END}")
    print(f"{Colors.INFO}Connection: {Colors.BOLD}{bridge.port}{Colors.END}")
    print(f"{Colors.SUCCESS}Type 'exit' to quit, 'help' for commands.{Colors.END}\n")
    
    while True:
        try:
            # Gruvbox Styled Prompt
            prompt = f"{Colors.PROMPT}{Colors.BOLD}mimic{Colors.END} {Colors.TEXT}>{Colors.END} "
            cmd = input(prompt).strip()
            
            if not cmd:
                continue
            if cmd.lower() in ["exit", "quit"]:
                break
            
            if cmd.lower() == "help":
                print(f"\n{Colors.INFO}{Colors.BOLD}Common Commands:{Colors.END}")
                print(f"{Colors.TEXT}  STATUS       - Show board status{Colors.END}")
                print(f"{Colors.TEXT}  VERSION      - Show firmware version{Colors.END}")
                print(f"{Colors.TEXT}  PIN_HIGH A5  - Set pin high{Colors.END}")
                print(f"{Colors.TEXT}  RESET        - Soft reboot the board{Colors.END}\n")
                continue

            # Execute on hardware
            response = bridge.execute(cmd)
            
            for line in response:
                if "OK" in line.upper():
                    print(f"{Colors.SUCCESS}{line}{Colors.END}")
                elif "ERROR" in line.upper():
                    print(f"{Colors.ERROR}{line}{Colors.END}")
                else:
                    print(f"{Colors.INFO}{line}{Colors.END}")
                    
        except KeyboardInterrupt:
            print(f"\n{Colors.INFO}Exiting terminal...{Colors.END}")
            break
        except EOFError:
            break
        except Exception as e:
            print(f"{Colors.ERROR}System Error: {e}{Colors.END}")

def main():
    parser = argparse.ArgumentParser(description="Mimic Firmware: Simulation & Control Tool")
    parser.add_argument("sensor", nargs="?", choices=["mpu6050", "bmp280", "gps"], help="Sensor to simulate")
    parser.add_argument("--port", help="Specify serial port (default: auto-detect)")
    parser.add_argument("--protocol", choices=["i2c", "spi", "uart"], help="Force a specific protocol")
    
    args = parser.parse_args()
    
    # Gruvbox search status
    print(f"{Colors.PROMPT}Searching for Mimic hardware...{Colors.END}")
    
    bridge = MimicBridge(port=args.port)
    if not bridge.connect():
        print(f"{Colors.ERROR}{Colors.BOLD}Error: Could not find or connect to Mimic board.{Colors.END}")
        sys.exit(1)
        
    if args.sensor:
        if args.sensor == "mpu6050":
            sim = MPU6050Simulator(bridge)
            print(f"\n{Colors.SUCCESS}{Colors.BOLD}Starting MPU6050 simulation on {bridge.port}{Colors.END}")
            print(f"{Colors.INFO}SCL -> PB6, SDA -> PB7.{Colors.END}")
            print(f"{Colors.PROMPT}Press Ctrl+C to exit.{Colors.END}")
            sim.start()
        elif args.sensor == "bmp280":
            protocol = args.protocol if args.protocol else "spi"
            sim = BMP280Simulator(bridge, protocol=protocol)
            print(f"\n{Colors.SUCCESS}{Colors.BOLD}Starting BMP280 simulation on {bridge.port}{Colors.END}")
            if protocol == "spi":
                print(f"{Colors.INFO}SPI: CS->PA4, SCK->PA5, MISO->PA6, MOSI->PA7{Colors.END}")
            else:
                print(f"{Colors.INFO}I2C: SCL->PB6, SDA->PB7 (Address 0x76).{Colors.END}")
            print(f"{Colors.PROMPT}Press Ctrl+C to exit.{Colors.END}")
            sim.start()
        elif args.sensor == "gps":
            sim = GPSSimulator(bridge)
            print(f"\n{Colors.SUCCESS}{Colors.BOLD}Starting GPS simulation (NMEA UART) on {bridge.port}{Colors.END}")
            print(f"{Colors.INFO}UART6 (9600 Baud): TX->PA11, RX->PA12{Colors.END}")
            print(f"{Colors.PROMPT}Press Ctrl+C to exit.{Colors.END}")
            sim.start()
    else:
        start_interactive_shell(bridge)

if __name__ == "__main__":
    main()
