#!/usr/bin/env python3
"""
MIMIC Host Interface
====================
Python CLI for controlling STM32 via MIMIC firmware.

Usage:
    python Mimic.py                    # Interactive mode
    python Mimic.py -c "PIN_HIGH D12"  # Single command
    python Mimic.py --port /dev/ttyUSB1 --baud 115200

Commands:
    GPIO:  PIN_STATUS, PIN_SET_OUT, PIN_SET_IN, PIN_HIGH, PIN_LOW, 
           PIN_READ, PIN_TOGGLE, PIN_MODE
    UART:  UART_INIT, UART_SEND, UART_RECV, UART_STATUS
    SPI:   SPI_INIT, SPI_SEND, SPI_RECV, SPI_TRANSFER, SPI_CS, SPI_STATUS
    System: HELP, VERSION, STATUS, RESET

Author: Mimic Project
"""

import serial
import serial.tools.list_ports
import sys
import time
import argparse

# Try to import readline for command history (optional)
try:
    import readline
except ImportError:
    pass


class MimicHost:
    """Host interface for MIMIC firmware on STM32"""
    
    def __init__(self, port='/dev/ttyUSB0', baud=9600, timeout=1):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser = None
        self.connected = False
        self.char_delay = 0.005  # 5ms delay between characters for reliability
        
    def connect(self):
        """Connect to STM32"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout
            )
            time.sleep(0.3)
            self.ser.reset_input_buffer()
            self.connected = True
            return True
        except serial.SerialException as e:
            print(f"Error: Cannot connect to {self.port}: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from STM32"""
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.connected = False
    
    def send_command(self, cmd, wait_response=True):
        """Send command and optionally wait for response"""
        if not self.connected:
            return None
        
        # Send character by character for reliability at low baud rates
        for c in cmd:
            self.ser.write(c.encode())
            time.sleep(self.char_delay)
        
        self.ser.write(b'\r\n')
        
        if wait_response:
            # Check if this is a blocking UART_RECV command - need to wait longer
            if cmd.upper().startswith('UART_RECV'):
                # Parse timeout from command: UART_RECV <inst> <len> [timeout_ms]
                parts = cmd.split()
                timeout_ms = 1000  # default
                if len(parts) >= 4:
                    try:
                        timeout_ms = int(parts[3])
                    except:
                        pass
                # Wait for the timeout plus some buffer
                wait_time = (timeout_ms / 1000.0) + 2.0
                response = self._read_response(wait_time)
            else:
                time.sleep(0.1)  # Wait for processing
                response = self._read_response()
            return response
        return None
    
    def _read_response(self, max_wait=2.0):
        """Read response from STM32"""
        response = b''
        start_time = time.time()
        
        # Wait for data with timeout
        while (time.time() - start_time) < max_wait:
            if self.ser.in_waiting:
                response += self.ser.read(self.ser.in_waiting)
                time.sleep(0.05)
                # Check if we got the prompt (response complete)
                if response.endswith(b'> ') or response.endswith(b'>\r\n'):
                    break
            else:
                time.sleep(0.05)
        
        # Decode and clean response
        try:
            text = response.decode('utf-8', errors='ignore')
            # Remove echo and prompt
            lines = text.split('\r\n')
            # Skip first line (echo) and filter empty/prompt lines
            clean_lines = []
            for line in lines[1:]:
                line = line.strip()
                if line and line != '>':
                    clean_lines.append(line)
            return '\n'.join(clean_lines)
        except:
            return response.hex()
    
    def interactive(self):
        """Interactive command mode"""
        print("""
╔═══════════════════════════════════════════════════════════════╗
║               MIMIC - Hardware Control Interface              ║
║          Control STM32 peripherals via simple commands        ║
╚═══════════════════════════════════════════════════════════════╝
""")
        
        if not self.connect():
            return
        
        print(f"Connected to {self.port} @ {self.baud} baud")
        print("Type 'help' for commands, 'exit' to quit\n")
        
        # Get initial status
        response = self.send_command("VERSION")
        if response:
            print(response)
            print()
        
        while True:
            try:
                cmd = input("\033[92mmimic>\033[0m ").strip()
                
                if not cmd:
                    continue
                
                # Local commands (processed on host, not sent to MCU)
                if cmd.lower() in ['exit', 'quit', 'q']:
                    print("Goodbye!")
                    break
                elif cmd.lower() == 'clear':
                    print("\033[2J\033[H", end='')
                    continue
                elif cmd.lower() == 'reconnect':
                    self.disconnect()
                    if self.connect():
                        print("Reconnected!")
                    continue
                elif cmd.lower().startswith('port '):
                    self.port = cmd.split()[1]
                    self.disconnect()
                    if self.connect():
                        print(f"Connected to {self.port}")
                    continue
                elif cmd.lower().startswith('baud '):
                    self.baud = int(cmd.split()[1])
                    self.disconnect()
                    if self.connect():
                        print(f"Baud rate set to {self.baud}")
                    continue
                elif cmd.lower() == 'ports':
                    ports = serial.tools.list_ports.comports()
                    if ports:
                        for p in ports:
                            print(f"  {p.device}: {p.description}")
                    else:
                        print("  No serial ports found")
                    continue
                
                # LED shortcuts
                elif cmd.lower().startswith('led '):
                    parts = cmd.lower().split()
                    if len(parts) >= 2:
                        action = parts[1]
                        color = parts[2] if len(parts) > 2 else 'all'
                        result = self._led_command(action, color)
                        print(result)
                    else:
                        print("Usage: led <on|off> [green|orange|red|blue|all]")
                    continue
                
                # Send command to STM32
                response = self.send_command(cmd)
                if response:
                    print(response)
                    
            except KeyboardInterrupt:
                print("\nUse 'exit' to quit")
            except EOFError:
                break
        
        self.disconnect()
    
    def _led_command(self, action, color):
        """Control Discovery board LEDs"""
        leds = {
            'green': 'D12', 'orange': 'D13', 
            'red': 'D14', 'blue': 'D15',
            'all': ['D12', 'D13', 'D14', 'D15']
        }
        
        pins = leds.get(color, [color])
        if isinstance(pins, str):
            pins = [pins]
        
        for pin in pins:
            self.send_command(f"PIN_SET_OUT {pin}", wait_response=False)
            time.sleep(0.1)
            if action == 'on':
                self.send_command(f"PIN_HIGH {pin}", wait_response=False)
            else:
                self.send_command(f"PIN_LOW {pin}", wait_response=False)
            time.sleep(0.1)
        
        # Flush any responses
        time.sleep(0.2)
        if self.ser.in_waiting:
            self.ser.read(self.ser.in_waiting)
        
        return f"LED(s) {'ON' if action == 'on' else 'OFF'}: {', '.join(pins)}"


def list_ports():
    """List available serial ports"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found")
    else:
        print("\nAvailable ports:")
        for p in ports:
            print(f"  {p.device}: {p.description}")
    print()


def main():
    parser = argparse.ArgumentParser(
        description='MIMIC Host Interface for STM32',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python Mimic.py                           # Interactive mode
  python Mimic.py -c "PIN_HIGH D12"         # Single command  
  python Mimic.py -c "UART_INIT 1 115200"   # Init UART1
  python Mimic.py --list                    # List serial ports
  python Mimic.py -p /dev/ttyUSB1 -b 115200 # Custom port/baud
  python Mimic.py --led on                  # Turn all LEDs on
  python Mimic.py --led off                 # Turn all LEDs off
        """
    )
    
    parser.add_argument('-p', '--port', default='/dev/ttyUSB0',
                        help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('-b', '--baud', type=int, default=9600,
                        help='Baud rate (default: 9600)')
    parser.add_argument('-c', '--command', 
                        help='Execute single command and exit')
    parser.add_argument('--list', action='store_true',
                        help='List available serial ports')
    parser.add_argument('--led', choices=['on', 'off'],
                        help='Quick LED control (all LEDs)')
    
    args = parser.parse_args()
    
    if args.list:
        list_ports()
        return
    
    mimic = MimicHost(port=args.port, baud=args.baud)
    
    if args.led:
        if mimic.connect():
            result = mimic._led_command(args.led, 'all')
            print(result)
            mimic.disconnect()
        return
    
    if args.command:
        # Single command mode
        if mimic.connect():
            response = mimic.send_command(args.command)
            if response:
                print(response)
            mimic.disconnect()
    else:
        # Interactive mode
        mimic.interactive()


if __name__ == "__main__":
    main()
