#!/usr/bin/env python3
"""
Mimic - Interactive STM32 Command Interface
Enhanced CLI with autocomplete, syntax hints, and examples

A Python-based command-line interface for controlling STM32F411 Discovery board.
Provides easy access to GPIO, UART, and SPI peripherals via simple text commands.

Commands:
    GPIO:  PIN_STATUS, PIN_SET_OUT, PIN_SET_IN, PIN_HIGH, PIN_LOW, 
           PIN_READ, PIN_TOGGLE, PIN_MODE
    UART:  UART_INIT, UART_SEND, UART_RECV, UART_STATUS
    SPI:   SPI_INIT, SPI_SEND, SPI_RECV, SPI_TRANSFER, SPI_CS, SPI_STATUS
    System: HELP, VERSION, STATUS, RESET

Author: Mimic Project
"""

import serial
import argparse
import sys
import time
from prompt_toolkit import PromptSession
from prompt_toolkit.completion import WordCompleter, Completer, Completion
from prompt_toolkit.styles import Style
from prompt_toolkit.formatted_text import FormattedText
from prompt_toolkit.key_binding import KeyBindings
from prompt_toolkit.history import FileHistory
import os

# Command definitions with syntax and examples
COMMANDS = {
    # GPIO Commands
    'PIN_STATUS': {
        'syntax': 'PIN_STATUS <PIN>',
        'description': 'Show pin configuration and state',
        'examples': ['PIN_STATUS A5', 'PIN_STATUS D12', 'PIN_STATUS B3'],
        'category': 'GPIO'
    },
    'PIN_SET_OUT': {
        'syntax': 'PIN_SET_OUT <PIN>',
        'description': 'Configure pin as output',
        'examples': ['PIN_SET_OUT A5', 'PIN_SET_OUT LED'],
        'category': 'GPIO'
    },
    'PIN_SET_IN': {
        'syntax': 'PIN_SET_IN <PIN> [PULL]',
        'description': 'Configure pin as input with optional pull resistor',
        'examples': ['PIN_SET_IN A5', 'PIN_SET_IN B3 UP', 'PIN_SET_IN C7 DOWN'],
        'category': 'GPIO'
    },
    'PIN_HIGH': {
        'syntax': 'PIN_HIGH <PIN>',
        'description': 'Set output pin to HIGH (3.3V)',
        'examples': ['PIN_HIGH A5', 'PIN_HIGH LED'],
        'category': 'GPIO'
    },
    'PIN_LOW': {
        'syntax': 'PIN_LOW <PIN>',
        'description': 'Set output pin to LOW (0V)',
        'examples': ['PIN_LOW A5', 'PIN_LOW LED'],
        'category': 'GPIO'
    },
    'PIN_READ': {
        'syntax': 'PIN_READ <PIN>',
        'description': 'Read current state of pin',
        'examples': ['PIN_READ A5', 'PIN_READ BUTTON'],
        'category': 'GPIO'
    },
    'PIN_TOGGLE': {
        'syntax': 'PIN_TOGGLE <PIN>',
        'description': 'Toggle output pin state',
        'examples': ['PIN_TOGGLE A5', 'PIN_TOGGLE LED'],
        'category': 'GPIO'
    },
    'PIN_MODE': {
        'syntax': 'PIN_MODE <PIN> <MODE>',
        'description': 'Set pin mode (IN, OUT, AF, AN)',
        'examples': ['PIN_MODE A5 OUT', 'PIN_MODE B3 IN', 'PIN_MODE C7 AF'],
        'category': 'GPIO'
    },
    
    # UART Commands
    'UART_INIT': {
        'syntax': 'UART_INIT <1|6> <BAUD> [PARITY] [STOP]',
        'description': 'Initialize UART peripheral',
        'examples': ['UART_INIT 1 9600', 'UART_INIT 6 115200 N 1', 'UART_INIT 1 57600 E 2'],
        'category': 'UART'
    },
    'UART_SEND': {
        'syntax': 'UART_SEND <1|6> <DATA>',
        'description': 'Send data via UART',
        'examples': ['UART_SEND 1 Hello', 'UART_SEND 6 "Test Data"'],
        'category': 'UART'
    },
    'UART_RECV': {
        'syntax': 'UART_RECV <1|6> <LENGTH> [TIMEOUT]',
        'description': 'Receive data from UART',
        'examples': ['UART_RECV 1 10', 'UART_RECV 6 20 500'],
        'category': 'UART'
    },
    'UART_STATUS': {
        'syntax': 'UART_STATUS',
        'description': 'Show status of all UART peripherals',
        'examples': ['UART_STATUS'],
        'category': 'UART'
    },
    
    # SPI Commands
    'SPI_INIT': {
        'syntax': 'SPI_INIT <1-5> <MASTER|SLAVE> <SPEED> [CPOL] [CPHA] [SIZE] [ORDER] [CS_PIN]',
        'description': 'Initialize SPI peripheral with automatic CS control',
        'examples': [
            'SPI_INIT 1 MASTER 1000000',
            'SPI_INIT 1 MASTER 1000000 0 0 8 MSB A4',
            'SPI_INIT 2 MASTER 500000 1 1 16 LSB B12'
        ],
        'category': 'SPI'
    },
    'SPI_SEND': {
        'syntax': 'SPI_SEND <1-5> <HEX_DATA>',
        'description': 'Send data via SPI (half-duplex TX)',
        'examples': ['SPI_SEND 1 AA BB CC DD', 'SPI_SEND 2 01 02 03'],
        'category': 'SPI'
    },
    'SPI_RECV': {
        'syntax': 'SPI_RECV <1-5> <LENGTH> [TIMEOUT]',
        'description': 'Receive data from SPI (half-duplex RX)',
        'examples': ['SPI_RECV 1 4', 'SPI_RECV 2 8 500'],
        'category': 'SPI'
    },
    'SPI_TRANSFER': {
        'syntax': 'SPI_TRANSFER <1-5> <HEX_DATA>',
        'description': 'Full-duplex SPI transfer (simultaneous TX/RX)',
        'examples': ['SPI_TRANSFER 1 AA BB CC DD', 'SPI_TRANSFER 2 FF EE DD CC'],
        'category': 'SPI'
    },
    'SPI_CS': {
        'syntax': 'SPI_CS <PIN> <HIGH|LOW>',
        'description': 'Manually control chip select pin',
        'examples': ['SPI_CS A4 LOW', 'SPI_CS B12 HIGH'],
        'category': 'SPI'
    },
    'SPI_STATUS': {
        'syntax': 'SPI_STATUS',
        'description': 'Show status of all SPI peripherals',
        'examples': ['SPI_STATUS'],
        'category': 'SPI'
    },

    # I2C Commands
    'I2C_INIT': {
        'syntax': 'I2C_INIT <1-3> <MASTER|SLAVE> <SPEED|ADDR>',
        'description': 'Initialize I2C peripheral',
        'examples': ['I2C_INIT 1 MASTER 100000', 'I2C_INIT 1 SLAVE 0x30'],
        'category': 'I2C'
    },
    'I2C_SCAN': {
        'syntax': 'I2C_SCAN <1-3>',
        'description': 'Scan I2C bus (Master only)',
        'examples': ['I2C_SCAN 1'],
        'category': 'I2C'
    },
    'I2C_WRITE': {
        'syntax': 'I2C_WRITE <1-3> <ADDR> <HEX_DATA>',
        'description': 'Write data (Master: to Slave / Slave: to Master)',
        'examples': ['I2C_WRITE 1 0x68 6B 00'],
        'category': 'I2C'
    },
    'I2C_READ': {
        'syntax': 'I2C_READ <1-3> <ADDR> <LENGTH>',
        'description': 'Read data (Master: from Slave / Slave: from Master)',
        'examples': ['I2C_READ 1 0x68 6'],
        'category': 'I2C'
    },
    'I2C_WRITE_READ': {
        'syntax': 'I2C_WRITE_READ <1-3> <ADDR> <HEX_DATA> <READ_LEN>',
        'description': 'Write command/address then read (Repeated Start)',
        'examples': ['I2C_WRITE_READ 1 0x68 75 1', 'I2C_WRITE_READ 2 0x50 00 10'],
        'category': 'I2C'
    },
    'I2C_STATUS': {
        'syntax': 'I2C_STATUS',
        'description': 'Show status of all I2C peripherals',
        'examples': ['I2C_STATUS'],
        'category': 'I2C'
    },
    
    # System Commands
    'HELP': {
        'syntax': 'HELP [COMMAND]',
        'description': 'Show help for all commands or specific command',
        'examples': ['HELP', 'HELP SPI_INIT', 'HELP GPIO'],
        'category': 'System'
    },
    'VERSION': {
        'syntax': 'VERSION',
        'description': 'Show firmware version',
        'examples': ['VERSION'],
        'category': 'System'
    },
    'STATUS': {
        'syntax': 'STATUS',
        'description': 'Show system status',
        'examples': ['STATUS'],
        'category': 'System'
    },
    'RESET': {
        'syntax': 'RESET',
        'description': 'Reset the microcontroller',
        'examples': ['RESET'],
        'category': 'System'
    }
}

# Shortcuts
SHORTCUTS = {
    'PS': 'PIN_STATUS',
    'PSO': 'PIN_SET_OUT',
    'PSI': 'PIN_SET_IN',
    'PH': 'PIN_HIGH',
    'PL': 'PIN_LOW',
    'PR': 'PIN_READ',
    'PT': 'PIN_TOGGLE',
    'PM': 'PIN_MODE',
    'UI': 'UART_INIT',
    'US': 'UART_SEND',
    'UR': 'UART_RECV',
    'SI': 'SPI_INIT',
    'SS': 'SPI_SEND',
    'SR': 'SPI_RECV',
    'ST': 'SPI_TRANSFER',
    'SCS': 'SPI_CS',
    'II': 'I2C_INIT',
    'IS': 'I2C_SCAN',
    'IW': 'I2C_WRITE',
    'IR': 'I2C_READ',
    'IWR': 'I2C_WRITE_READ'
}


class MimicCompleter(Completer):
    """Custom completer with command hints and examples"""
    
    def get_completions(self, document, complete_event):
        word = document.get_word_before_cursor()
        line = document.text_before_cursor
        
        # Complete command names
        if ' ' not in line or line.strip() == word:
            for cmd in COMMANDS.keys():
                if cmd.startswith(word.upper()):
                    yield Completion(
                        cmd,
                        start_position=-len(word),
                        display=cmd,
                        display_meta=COMMANDS[cmd]['description']
                    )
            
            # Also suggest shortcuts
            for shortcut, full_cmd in SHORTCUTS.items():
                if shortcut.startswith(word.upper()):
                    yield Completion(
                        shortcut,
                        start_position=-len(word),
                        display=f"{shortcut} → {full_cmd}",
                        display_meta=COMMANDS[full_cmd]['description']
                    )


def get_bottom_toolbar(current_text=''):
    """Generate bottom toolbar with syntax hints"""
    from prompt_toolkit.formatted_text import FormattedText
    
    if not current_text:
        return FormattedText([('', 'Type a command or press TAB for autocomplete')])
    
    # Extract command from current text
    cmd = current_text.strip().split()[0].upper()
    
    # Check if it's a shortcut
    if cmd in SHORTCUTS:
        cmd = SHORTCUTS[cmd]
    
    if cmd in COMMANDS:
        info = COMMANDS[cmd]
        syntax = info['syntax']
        example = info['examples'][0] if info['examples'] else ''
        
        return FormattedText([
            ('', 'Syntax: '),
            ('ansicyan', syntax),
            ('', '  Example: '),
            ('ansigreen', example)
        ])
    
    return FormattedText([('', 'Unknown command - press TAB for suggestions')])


class MimicCLI:
    def __init__(self, port, baudrate=9600):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.session = None
        
        # Create style
        self.style = Style.from_dict({
            'prompt': '#00aa00 bold',
            'command': '#00aaaa',
        })
        
        # Setup history file
        history_file = os.path.expanduser('~/.mimic_history')
        
        # Create prompt session with autocomplete
        self.session = PromptSession(
            completer=MimicCompleter(),
            style=self.style,
            bottom_toolbar=lambda: get_bottom_toolbar(self.session.default_buffer.text if self.session else ''),
            complete_while_typing=True,
            history=FileHistory(history_file)
        )
    
    def connect(self):
        """Connect to STM32"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(0.5)  # Wait for connection
            print(f"✓ Connected to {self.port} at {self.baudrate} baud")
            return True
        except serial.SerialException as e:
            print(f"✗ Failed to connect: {e}")
            return False
    
    def send_command(self, command):
        """Send command to STM32 and get response"""
        if not self.ser or not self.ser.is_open:
            print("✗ Not connected to device")
            return
        
        try:
            # Send command
            self.ser.write((command + '\n').encode())
            
            # Read response
            response = []
            timeout = time.time() + 2  # 2 second timeout
            
            while time.time() < timeout:
                if self.ser.in_waiting:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        response.append(line)
                        # Check if this looks like the end of response
                        if line.startswith('OK:') or line.startswith('ERROR:') or line.startswith('UNKNOWN'):
                            time.sleep(0.05)  # Small delay for any trailing data
                            break
                time.sleep(0.01)
            
            # Print response
            for line in response:
                print(line)
                
        except Exception as e:
            print(f"✗ Error: {e}")
    
    def show_help(self, category=None):
        """Show help for commands"""
        if category and category.upper() in COMMANDS:
            # Show help for specific command
            cmd = category.upper()
            info = COMMANDS[cmd]
            print(f"\n{cmd}")
            print(f"  {info['description']}")
            print(f"\n  Syntax: {info['syntax']}")
            print(f"\n  Examples:")
            for ex in info['examples']:
                print(f"    {ex}")
            print()
        else:
            # Show all commands by category
            categories = {}
            for cmd, info in COMMANDS.items():
                cat = info['category']
                if cat not in categories:
                    categories[cat] = []
                categories[cat].append((cmd, info))
            
            print("\n╔══════════════════════════════════════════════════════════╗")
            print("║           MIMIC - STM32 Command Interface               ║")
            print("╚══════════════════════════════════════════════════════════╝\n")
            
            for cat in ['GPIO', 'UART', 'SPI', 'I2C', 'System']:
                if cat in categories:
                    print(f"[{cat} Commands]")
                    for cmd, info in categories[cat]:
                        print(f"  {cmd:20} - {info['description']}")
                    print()
            
            print("[Shortcuts]")
            for short, full in sorted(SHORTCUTS.items()):
                print(f"  {short:5} → {full}")
            print()
            print("Type 'HELP <command>' for detailed help on a specific command")
            print("Press TAB for autocomplete and hints\n")
    
    def run(self):
        """Main CLI loop"""
        if not self.connect():
            return
        
        print("\n╔══════════════════════════════════════════════════════════╗")
        print("║           MIMIC - Interactive CLI v2.0                  ║")
        print("║           Enhanced with Autocomplete & Hints            ║")
        print("╚══════════════════════════════════════════════════════════╝")
        print("\nType 'help' for commands, TAB for autocomplete, Ctrl+C to exit\n")
        
        try:
            while True:
                try:
                    # Get command with autocomplete
                    command = self.session.prompt(
                        FormattedText([('ansigreen bold', 'mimic> ')]),
                        bottom_toolbar=lambda: get_bottom_toolbar(
                            self.session.default_buffer.text if self.session else ''
                        )
                    ).strip()
                    
                    if not command:
                        continue
                    
                    # Handle local commands
                    if command.lower() == 'exit' or command.lower() == 'quit':
                        break
                    elif command.lower() == 'clear':
                        os.system('clear' if os.name != 'nt' else 'cls')
                        continue
                    elif command.lower().startswith('help'):
                        parts = command.split()
                        if len(parts) > 1:
                            self.show_help(parts[1])
                        else:
                            self.show_help()
                        continue
                    
                    # Send to STM32
                    self.send_command(command)
                    
                except KeyboardInterrupt:
                    continue
                except EOFError:
                    break
                    
        except KeyboardInterrupt:
            print("\n")
        finally:
            if self.ser and self.ser.is_open:
                self.ser.close()
            print("Disconnected.")


def main():
    parser = argparse.ArgumentParser(
        description='Mimic - Enhanced Interactive STM32 CLI',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --port /dev/ttyUSB0
  %(prog)s --port COM3 --baud 115200
  
Features:
  - TAB autocomplete for commands
  - Real-time syntax hints
  - Command examples in toolbar
  - Command history (up/down arrows)
  - Shortcuts for common commands
        """
    )
    
    parser.add_argument('--port', '-p', required=True, help='Serial port (e.g., /dev/ttyUSB0, COM3)')
    parser.add_argument('--baud', '-b', type=int, default=9600, help='Baud rate (default: 9600)')
    
    args = parser.parse_args()
    
    cli = MimicCLI(args.port, args.baud)
    cli.run()


if __name__ == '__main__':
    main()
