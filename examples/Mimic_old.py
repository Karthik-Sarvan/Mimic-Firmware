#!/usr/bin/env python3
"""
STM32F411 Discovery Board - Direct GPIO Control & Protocol Testing via ST-LINK & GDB
Simple scripting interface for hardware register manipulation and communication protocols
"""

import subprocess
import os
import sys
import re
from protocols.uart import UART

# GPIO Port base addresses (STM32F411)
PORTS = {
    "A": {"base": 0x40020000, "rcc_bit": 0},
    "B": {"base": 0x40020400, "rcc_bit": 1},
    "C": {"base": 0x40020800, "rcc_bit": 2},
    "D": {"base": 0x40020C00, "rcc_bit": 3},
    "E": {"base": 0x40021000, "rcc_bit": 4},
}

# Register offsets for GPIO
GPIO_MODER = 0x00      # Mode register
GPIO_OTYPER = 0x04    # Output type register
GPIO_OSPEEDR = 0x08   # Output speed register
GPIO_PUPDR = 0x0C     # Pull-up/pull-down register
GPIO_IDR = 0x10       # Input data register
GPIO_ODR = 0x14       # Output data register
GPIO_BSRR = 0x18      # Bit set/reset register
RCC_AHB1ENR = 0x40023830  # RCC AHB1 Enable Register

def gdb_connect():
    """Start GDB connection with OpenOCD"""
    return """
set confirm off
target extended-remote :3333
monitor reset halt
"""

def gdb_disconnect():
    """Disconnect from GDB"""
    return "quit\n"

def gpio_configure_output(port, pin):
    """Configure GPIO pin as output (push-pull, no pull)"""
    if port.upper() not in PORTS:
        return None
    
    port_info = PORTS[port.upper()]
    base = port_info["base"]
    rcc_bit = port_info["rcc_bit"]
    
    # Enable clock: RCC_AHB1ENR |= (1 << rcc_bit)
    enable_clock = f"set $temp = *(unsigned int*){hex(RCC_AHB1ENR)}\nset $temp = $temp | (1 << {rcc_bit})\nset *(unsigned int*){hex(RCC_AHB1ENR)} = $temp\n"
    
    # Configure as output: MODER |= (0x1 << (pin*2))
    moder_offset = GPIO_MODER
    config_pin = f"set $temp = *(unsigned int*){hex(base + moder_offset)}\nset $temp = ($temp & ~(3 << ({pin} * 2))) | (1 << ({pin} * 2))\nset *(unsigned int*){hex(base + moder_offset)} = $temp\n"
    
    return enable_clock + config_pin

def gpio_configure_input(port, pin):
    """Configure GPIO pin as input (no pull)"""
    if port.upper() not in PORTS:
        return None
    
    port_info = PORTS[port.upper()]
    base = port_info["base"]
    rcc_bit = port_info["rcc_bit"]
    
    # Enable clock: RCC_AHB1ENR |= (1 << rcc_bit)
    enable_clock = f"set $temp = *(unsigned int*){hex(RCC_AHB1ENR)}\nset $temp = $temp | (1 << {rcc_bit})\nset *(unsigned int*){hex(RCC_AHB1ENR)} = $temp\n"
    
    # Configure as input: MODER &= ~(3 << (pin*2)) - clears the bits to 0 (input mode)
    moder_offset = GPIO_MODER
    config_pin = f"set $temp = *(unsigned int*){hex(base + moder_offset)}\nset $temp = $temp & ~(3 << ({pin} * 2))\nset *(unsigned int*){hex(base + moder_offset)} = $temp\n"
    
    return enable_clock + config_pin

def gpio_set(port, pin):
    """Set GPIO pin HIGH"""
    if port.upper() not in PORTS:
        return None
    
    base = PORTS[port.upper()]["base"]
    # BSRR: write 1 to set pin
    return f"set *(unsigned int*){hex(base + GPIO_BSRR)} = (1 << {pin})\n"

def gpio_clear(port, pin):
    """Set GPIO pin LOW"""
    if port.upper() not in PORTS:
        return None
    
    base = PORTS[port.upper()]["base"]
    # BSRR: write 1 to bit [16+pin] to clear
    return f"set *(unsigned int*){hex(base + GPIO_BSRR)} = (1 << ({pin} + 16))\n"

def gpio_read(port, pin):
    """Read GPIO pin value"""
    if port.upper() not in PORTS:
        return None
    
    base = PORTS[port.upper()]["base"]
    return f"printf \"Pin value: %d\\n\", (*(unsigned int*){hex(base + GPIO_IDR)} >> {pin}) & 1"

def gpio_toggle(port, pin):
    """Toggle GPIO pin"""
    if port.upper() not in PORTS:
        return None
    
    base = PORTS[port.upper()]["base"]
    # Read ODR, XOR with pin bit, write back
    return f"set $temp = *(unsigned int*){hex(base + GPIO_ODR)}\nset $temp = $temp ^ (1 << {pin})\nset *(unsigned int*){hex(base + GPIO_ODR)} = $temp\n"

def gpio_info(port, pin):
    """Read GPIO pin configuration"""
    if port.upper() not in PORTS:
        return None
    
    port_upper = port.upper()
    base = PORTS[port_upper]["base"]
    
    # Read MODER, OTYPER, OSPEEDR, PUPDR, ODR, IDR
    gdb_script = f"""
set $base = {hex(base)}
set $moder = *(unsigned int*)($base + 0x00)
set $otyper = *(unsigned int*)($base + 0x04)
set $ospeedr = *(unsigned int*)($base + 0x08)
set $pupdr = *(unsigned int*)($base + 0x0C)
set $odr = *(unsigned int*)($base + 0x14)
set $idr = *(unsigned int*)($base + 0x10)

set $mode = ($moder >> ({pin} * 2)) & 3
set $type = ($otyper >> {pin}) & 1
set $speed = ($ospeedr >> ({pin} * 2)) & 3
set $pull = ($pupdr >> ({pin} * 2)) & 3
set $out = ($odr >> {pin}) & 1
set $in = ($idr >> {pin}) & 1

printf "GPIO {port_upper}{pin} Configuration:\\n"
printf "Mode: %d", $mode
printf "  Type: %d", $type
printf "  Speed: %d", $speed
printf "  Pull: %d", $pull
printf "  Output: %d", $out
printf "  Input: %d\\n", $in
"""
    return gdb_script

def system_reset():
    """Reset the STM32F411 system"""
    return "monitor reset\nprintf \"System reset complete\\n\""

def translate_command(cmd_str):
    """Translate human-readable command to GDB script"""
    parts = cmd_str.split()
    
    if len(parts) < 1:
        return None
    
    action = parts[0].lower()
    
    # gpio_out A 5 - configure as output
    if action == "gpio_out":
        if len(parts) != 3:
            return None
        port, pin = parts[1], int(parts[2])
        if pin < 0 or pin > 15:
            return None
        return gpio_configure_output(port, pin)
    
    # gpio_in A 5 - configure as input
    elif action == "gpio_in":
        if len(parts) != 3:
            return None
        port, pin = parts[1], int(parts[2])
        if pin < 0 or pin > 15:
            return None
        return gpio_configure_input(port, pin)
    
    # gpio_on A 5 - set HIGH
    elif action == "gpio_on":
        if len(parts) != 3:
            return None
        port, pin = parts[1], int(parts[2])
        if pin < 0 or pin > 15:
            return None
        return gpio_set(port, pin)
    
    # gpio_off A 5 - set LOW
    elif action == "gpio_off":
        if len(parts) != 3:
            return None
        port, pin = parts[1], int(parts[2])
        if pin < 0 or pin > 15:
            return None
        return gpio_clear(port, pin)
    
    # gpio_read A 5 - read pin
    elif action == "gpio_read" or action == "gpio_get":
        if len(parts) != 3:
            return None
        port, pin = parts[1], int(parts[2])
        if pin < 0 or pin > 15:
            return None
        return gpio_read(port, pin)
    
    # gpio_toggle A 5 - toggle pin
    elif action == "gpio_toggle":
        if len(parts) != 3:
            return None
        port, pin = parts[1], int(parts[2])
        if pin < 0 or pin > 15:
            return None
        return gpio_toggle(port, pin)
    
    # gpio_info A 5 - read pin configuration
    elif action == "gpio_info":
        if len(parts) != 3:
            return None
        port, pin = parts[1], int(parts[2])
        if pin < 0 or pin > 15:
            return None
        return gpio_info(port, pin)
    
    # system_reset - reset the board
    elif action == "system_reset" or action == "reset":
        return system_reset()
    
    # UART commands - return tuple (action, args) for special handling
    elif action.startswith("uart_"):
        return (action, parts)
    
    # help - show commands
    elif action == "help":
        return None  # Special case handled in main
    
    return None

def parse_uart_config(args):
    """
    Parse UART command arguments and extract configuration
    
    Args:
        args: List of arguments from command
        
    Returns:
        tuple: (success, config_dict)
    """
    config = {
        'baud': 9600,              # Default baud rate
        'data_format': 'ascii',    # Default data format
        'frame': 'standard',       # Default frame format
        'timeout': 1000            # Default timeout in ms
    }
    
    # Extract config parameters from args (key=value format)
    for arg in args[3:]:  # Skip action, tx_port, tx_pin, rx_port, rx_pin
        if '=' in arg:
            key, value = arg.split('=', 1)
            key = key.strip().lower()
            value = value.strip()
            
            if key == 'baud':
                try:
                    config['baud'] = int(value)
                except ValueError:
                    return False, f"Invalid baud rate: {value}"
            elif key == 'data_format' or key == 'format':
                if value.lower() not in ['ascii', 'hex']:
                    return False, f"Invalid format: {value}. Use 'ascii' or 'hex'"
                config['data_format'] = value.lower()
            elif key == 'frame':
                config['frame'] = value.lower()
            elif key == 'timeout':
                try:
                    config['timeout'] = int(value)
                except ValueError:
                    return False, f"Invalid timeout: {value}"
    
    return True, config

def handle_uart_command(cmd_str):
    """
    Handle UART protocol commands
    
    Commands:
        uart_send <TX_PORT> <TX_PIN> <data> [baud=9600] [format=ascii] [frame=standard]
        uart_recv <RX_PORT> <RX_PIN> [timeout=1000] [baud=9600] [format=ascii]
        uart_monitor <RX_PORT> <RX_PIN> [duration=10000] [interval=100] [baud=9600]
        uart_config <TX_PORT> <TX_PIN> <RX_PORT> <RX_PIN>  - Show UART configuration
    
    Returns:
        str: Output message
    """
    action = cmd_str[0].lower()
    parts = cmd_str[1]  # Already a list from main()
    
    try:
        if action == "uart_send":
            # uart_send A 9 "Hello" baud=9600 format=ascii
            if len(parts) < 4:
                return "Usage: uart_send <TX_PORT> <TX_PIN> <data> [baud=9600] [format=ascii]"
            
            tx_port = parts[1]
            try:
                tx_pin = int(parts[2])
            except ValueError:
                return "Invalid TX pin"
            
            # Find the data - it's everything between pin and first key=value
            data_start_idx = 3
            data_parts = []
            config_args = []
            
            for i, arg in enumerate(parts[3:], 3):
                if '=' in arg:
                    config_args = parts[i:]
                    break
                else:
                    data_parts.append(arg)
            
            if not data_parts:
                return "No data to send"
            
            data = ' '.join(data_parts)
            
            # Parse configuration
            success, config = parse_uart_config(['uart_send', tx_port, str(tx_pin)] + config_args)
            if not success:
                return config
            
            # For send, we just need TX pin, so use same port for RX (won't be used)
            try:
                uart = UART(tx_port, tx_pin, tx_port, tx_pin, **config)
            except Exception as e:
                return f"Error creating UART: {str(e)}"
            
            # Remove quotes if present
            data = data.strip('"\'')
            
            try:
                if uart.send(data):
                    return f"Sent ({config['data_format']}): {data}\nBaud: {config['baud']}, Format: {config['data_format']}"
                else:
                    return "Send failed"
            except Exception as e:
                return f"Send error: {str(e)}"
        
        elif action == "uart_recv":
            # uart_recv A 10 baud=9600 format=ascii timeout=1000
            if len(parts) < 3:
                return "Usage: uart_recv <RX_PORT> <RX_PIN> [timeout=1000] [baud=9600] [format=ascii]"
            
            rx_port = parts[1]
            try:
                rx_pin = int(parts[2])
            except ValueError:
                return "Invalid RX pin"
            
            # Parse configuration
            success, config = parse_uart_config(['uart_recv', rx_port, str(rx_pin)] + parts[3:])
            if not success:
                return config
            
            timeout = config.pop('timeout', 1000)
            
            # For recv, we just need RX pin, so use same port for TX (won't be used)
            try:
                uart = UART(rx_port, rx_pin, rx_port, rx_pin, **config)
            except Exception as e:
                return f"Error creating UART: {str(e)}"
            
            try:
                data = uart.receive(timeout_ms=timeout)
                
                if data is None:
                    return f"Timeout waiting for data ({timeout}ms)"
                else:
                    return f"Received ({config['data_format']}): {data}"
            except Exception as e:
                return f"Receive error: {str(e)}"
        
        elif action == "uart_monitor":
            # uart_monitor A 10 duration=10000 interval=100 baud=9600
            if len(parts) < 3:
                return "Usage: uart_monitor <RX_PORT> <RX_PIN> [duration=10000] [interval=100] [baud=9600]"
            
            rx_port = parts[1]
            try:
                rx_pin = int(parts[2])
            except ValueError:
                return "Invalid RX pin"
            
            # Extract monitor-specific parameters
            duration = 10000
            interval = 100
            config_args = []
            
            for arg in parts[3:]:
                if arg.startswith('duration='):
                    try:
                        duration = int(arg.split('=')[1])
                    except ValueError:
                        pass
                elif arg.startswith('interval='):
                    try:
                        interval = int(arg.split('=')[1])
                    except ValueError:
                        pass
                else:
                    config_args.append(arg)
            
            success, config = parse_uart_config(['uart_monitor', rx_port, str(rx_pin)] + config_args)
            if not success:
                return config
            
            uart = UART(rx_port, rx_pin, rx_port, rx_pin, **config)
            
            print(f"Monitoring RX{rx_port}{rx_pin} for {duration}ms (interval: {interval}ms)")
            print(f"Baud: {config['baud']}, Format: {config['data_format']}")
            print("Press Ctrl+C to stop\n")
            
            results = uart.monitor(duration_ms=duration, interval_ms=interval)
            
            if results:
                return f"Captured {len(results)} message(s)"
            else:
                return "No data captured"
        
        elif action == "uart_config":
            # uart_config A 9 A 10 - Show UART configuration
            if len(parts) < 5:
                return "Usage: uart_config <TX_PORT> <TX_PIN> <RX_PORT> <RX_PIN> [baud=9600]"
            
            tx_port = parts[1]
            try:
                tx_pin = int(parts[2])
            except ValueError:
                return "Invalid TX pin"
            
            rx_port = parts[3]
            try:
                rx_pin = int(parts[4])
            except ValueError:
                return "Invalid RX pin"
            
            success, config = parse_uart_config(['uart_config', tx_port, str(tx_pin), rx_port, str(rx_pin)] + parts[5:])
            if not success:
                return config
            
            uart = UART(tx_port, tx_pin, rx_port, rx_pin, **config)
            uart_config = uart.get_config()
            
            output = "UART Configuration:\n"
            output += f"  TX Pin: {uart_config['tx_pin']}\n"
            output += f"  RX Pin: {uart_config['rx_pin']}\n"
            output += f"  Baud Rate: {uart_config['baud']}\n"
            output += f"  Data Format: {uart_config['data_format']}\n"
            output += f"  Frame Format: {uart_config['frame']}\n"
            output += f"  Bit Time: {uart_config['bit_time']*1000:.3f}ms\n"
            output += f"  Timeout: {config['timeout']}ms"
            
            return output
        
        else:
            return f"Unknown UART command: {action}"
    
    except Exception as e:
        return f"UART Error: {e}"

def run_gdb_command(gdb_script):
    """Execute GDB commands via OpenOCD"""
    # Build command list with separate -ex arguments
    cmd = ["arm-none-eabi-gdb", "-q", "-batch"]
    
    # Add connection - NO reset halt (it clears our GPIO config!)
    cmd.extend(["-ex", "set confirm off"])
    cmd.extend(["-ex", "target extended-remote :3333"])
    
    # Add user commands (split by lines and add as separate -ex args)
    for line in gdb_script.strip().split('\n'):
        line = line.strip()
        if line:
            cmd.extend(["-ex", line])
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=10
        )
        
        return result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return "", "ERROR: GDB command timeout (>10s) - check if OpenOCD is running"
    except FileNotFoundError:
        return "", "ERROR: arm-none-eabi-gdb not found. Install: sudo pacman -S arm-none-eabi-gdb"

def show_help():
    """Display help information"""
    help_text = """
STM32F411 Discovery - GPIO Control & Protocol Testing Shell
Direct Hardware Control via ST-LINK & GDB

=== GPIO CONTROL ===
  gpio_out <PORT> <PIN>       - Configure pin as output
  gpio_in <PORT> <PIN>        - Configure pin as input
  gpio_on <PORT> <PIN>        - Set pin HIGH
  gpio_off <PORT> <PIN>       - Set pin LOW
  gpio_toggle <PORT> <PIN>    - Toggle pin state
  gpio_read <PORT> <PIN>      - Read pin value
  gpio_info <PORT> <PIN>      - Read pin configuration

  Examples:
    gpio_out D 12              - Configure PD12 as output
    gpio_on D 12               - Turn on PD12
    gpio_read A 0              - Read PA0 (button)

=== UART COMMUNICATION (via GPIO Bit-Banging) ===
  uart_send <TX_PORT> <TX_PIN> <data> [baud=9600] [format=ascii]
  uart_recv <RX_PORT> <RX_PIN> [timeout=1000] [baud=9600] [format=ascii]
  uart_monitor <RX_PORT> <RX_PIN> [duration=10000] [interval=100] [baud=9600]
  uart_config <TX_PORT> <TX_PIN> <RX_PORT> <RX_PIN> [baud=9600]
  
  Parameters:
    baud        - Baud rate: 9600, 19200, 38400, 57600, 115200, 230400 (default: 9600)
    format      - Data format: 'ascii' or 'hex' (default: 'ascii')
    timeout     - Receive timeout in milliseconds (default: 1000)
    duration    - Monitor duration in milliseconds (default: 10000)
    interval    - Monitor check interval in milliseconds (default: 100)
  
  Examples:
    uart_config A 9 A 10 baud=9600
                                - Show UART config for TX=PA9, RX=PA10
    
    uart_send A 9 "Hello" baud=9600
                                - Send "Hello" via PA9 at 9600 baud
    
    uart_send A 9 "0xAA 0xBB" format=hex
                                - Send hex data via PA9
    
    uart_recv A 10 timeout=5000 baud=9600
                                - Receive on PA10, wait 5 seconds
    
    uart_monitor A 10 duration=30000 baud=9600
                                - Monitor RX on PA10 for 30 seconds

=== SYSTEM ===
  reset                        - Reset the STM32F411 board
  help                         - Show this help message
  exit                         - Exit the shell

=== PIN REFERENCE ===
  PORTS: A, B, C, D, E (case-insensitive)
  PINS:  0-15 (all GPIO pins)
  
  Common pins on STM32F411 Discovery:
    LEDs:   PD12 (Green), PD13 (Orange), PD14 (Red), PD15 (Blue)
    Button: PA0 (User Button)

=== GPIO CONFIGURATION REFERENCE ===
  Modes:  0=INPUT, 1=OUTPUT, 2=ALTERNATE, 3=ANALOG
  Types:  0=PUSH-PULL, 1=OPEN-DRAIN
  Speeds: 0=LOW, 1=MEDIUM, 2=FAST, 3=HIGH
  Pulls:  0=NONE, 1=PULL-UP, 2=PULL-DOWN

=== QUICK START ===
  1. gpio_config A 9 A 10 baud=9600
  2. uart_send A 9 "Test"
  3. uart_recv A 10

NOTES:
  - You can use ANY GPIO pins for UART (TX and RX)
  - Configuration parameters use defaults if not specified
  - Bit-banging is suitable for low baud rates (≤115200)
  - Format defaults to ASCII if not specified
  - Multiple parameters can be combined
"""
    print(help_text)

def main():
    """Main interactive shell"""
    print("\nSTM32F411 Discovery - GPIO Control & UART Testing Shell")
    print("Direct Hardware Control via ST-LINK & GDB\n")
    print("Type 'help' for commands, 'exit' to quit\n")
    
    while True:
        try:
            cmd = input("stm32> ").strip().replace('\r', '')
            
            # Handle empty input
            if not cmd:
                continue
            
            # Handle special commands
            if cmd.lower() == "exit" or cmd.lower() == "quit":
                print("Exiting.")
                break
            
            if cmd.lower() == "help":
                show_help()
                continue
            
            # Normalize whitespace
            cmd = " ".join(cmd.split())
            
            # Translate to GDB command
            gdb_cmd = translate_command(cmd)
            
            if gdb_cmd is None:
                print("ERROR: Invalid command. Type 'help' for usage.")
                continue
            
            # Handle UART commands (returned as tuple)
            if isinstance(gdb_cmd, tuple):
                action, parts = gdb_cmd
                output = handle_uart_command((action, parts))
                print(output)
                continue
            
            # Execute GDB command
            stdout, stderr = run_gdb_command(gdb_cmd)
            
            # Check for errors
            if "error" in stderr.lower() or "timeout" in stderr.lower():
                print(f"ERROR: {stderr}")
            elif "not found" in stderr.lower():
                print(f"ERROR: {stderr}")
            elif stderr and "Could not connect" in stderr:
                print("ERROR: Cannot connect to OpenOCD")
                print("Make sure OpenOCD is running:")
                print("openocd -f board/st_stm32f4_discovery.cfg")
            else:
                # Show output if it's a read/info/reset operation
                if "gpio_read" in cmd or "gpio_get" in cmd:
                    # Extract pin value from GDB output
                    for line in stdout.split('\n'):
                        if "Pin value:" in line:
                            print(line.strip())
                            break
                    else:
                        # Fallback if output format changed
                        if stdout.strip():
                            print(stdout.strip())
                        else:
                            print("OK")
                elif "gpio_info" in cmd:
                    # Show GPIO configuration info
                    if stdout.strip():
                        print(stdout.strip())
                    else:
                        print("OK")
                elif "reset" in cmd or "system_reset" in cmd:
                    # Show reset confirmation
                    for line in stdout.split('\n'):
                        if "reset" in line.lower():
                            print(line.strip())
                            break
                    else:
                        print("System reset complete")
                else:
                    # Regular command, just show OK
                    print("OK")
        
        except KeyboardInterrupt:
            print("\nExiting.")
            break
        except EOFError:
            print("\nExiting.")
            break
        except Exception as e:
            print(f"ERROR: {e}")

if __name__ == "__main__":
    main()
