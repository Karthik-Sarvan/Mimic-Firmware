import sys
import click
import logging
import importlib

try:
    import readline
except ImportError:
    pass 

from mimic import MimicBridge

class Colors:
    PROMPT = '\033[38;5;109m'
    SUCCESS = '\033[38;5;142m'
    INFO = '\033[38;5;214m'
    ERROR = '\033[38;5;167m'
    TEXT = '\033[38;5;223m'
    BOLD = '\033[1m'
    END = '\033[0m'

def start_interactive_shell(bridge: MimicBridge):
    click.echo(f"\n{Colors.PROMPT}{Colors.BOLD}{'='*55}{Colors.END}")
    click.echo(f"{Colors.PROMPT}{Colors.BOLD}      MIMIC v1.1.0 - GRUVBOX HARDWARE TERMINAL{Colors.END}")
    click.echo(f"{Colors.PROMPT}{Colors.BOLD}{'='*55}{Colors.END}")
    click.echo(f"{Colors.INFO}Connection: {Colors.BOLD}{bridge.port}{Colors.END}")
    click.echo(f"{Colors.SUCCESS}Type 'exit' to quit, 'help' for commands.{Colors.END}\n")
    
    while True:
        try:
            prompt_str = f"{Colors.PROMPT}{Colors.BOLD}mimic{Colors.END} {Colors.TEXT}>{Colors.END} "
            cmd = input(prompt_str).strip()
            
            if not cmd:
                continue
            if cmd.lower() in ["exit", "quit"]:
                break
            
            if cmd.lower() == "help":
                click.echo(f"\n{Colors.INFO}{Colors.BOLD}Common Commands:{Colors.END}")
                click.echo(f"{Colors.TEXT}  STATUS       - Show board status{Colors.END}")
                click.echo(f"{Colors.TEXT}  VERSION      - Show firmware version{Colors.END}")
                click.echo(f"{Colors.TEXT}  PIN_HIGH A5  - Set pin high{Colors.END}")
                click.echo(f"{Colors.TEXT}  RESET        - Soft reboot the board{Colors.END}\n")
                continue

            response = bridge.execute(cmd)
            for line in response:
                if "OK" in line.upper():
                    click.echo(f"{Colors.SUCCESS}{line}{Colors.END}")
                elif "ERROR" in line.upper():
                    click.echo(f"{Colors.ERROR}{line}{Colors.END}")
                else:
                    click.echo(f"{Colors.INFO}{line}{Colors.END}")
                    
        except KeyboardInterrupt:
            click.echo(f"\n{Colors.INFO}Exiting terminal...{Colors.END}")
            break
        except EOFError:
            break
        except Exception as e:
            click.echo(f"{Colors.ERROR}System Error: {e}{Colors.END}")

@click.group(invoke_without_command=True)
@click.option('--port', help="Specify serial port (default: auto-detect)")
@click.pass_context
def cli(ctx, port):
    """Mimic Firmware: Simulation & Control Tool"""
    if ctx.invoked_subcommand is None:
        click.echo(f"{Colors.PROMPT}Searching for Mimic hardware...{Colors.END}")
        bridge = MimicBridge(port=port)
        if not bridge.connect():
            click.echo(f"{Colors.ERROR}{Colors.BOLD}Error: Could not find or connect to Mimic board.{Colors.END}")
            sys.exit(1)
        start_interactive_shell(bridge)
    else:
        ctx.ensure_object(dict)
        ctx.obj['PORT'] = port

@cli.command()
@click.argument('sensor', type=click.Choice(['mpu6050', 'bmp280', 'gps']))
@click.option('--protocol', type=click.Choice(['i2c', 'spi', 'uart']), help="Force a specific protocol")
@click.pass_context
def simulate(ctx, sensor, protocol):
    """Start sensor simulation (requires mimic-sensors to be installed)"""
    port = ctx.obj.get('PORT')
    bridge = MimicBridge(port=port)
    if not bridge.connect():
        click.echo(f"{Colors.ERROR}{Colors.BOLD}Error: Could not find or connect to Mimic board.{Colors.END}")
        sys.exit(1)
        
    try:
        sensor_module = importlib.import_module(f"mimic_sensors.{sensor}")
    except ImportError:
        click.echo(f"{Colors.ERROR}Error: The 'mimic_sensors' package is not installed.{Colors.END}")
        click.echo("Please install the sensors repository to use this feature.")
        sys.exit(1)

    if sensor == "mpu6050":
        sim = sensor_module.MPU6050Simulator(bridge)
        click.echo(f"\n{Colors.SUCCESS}{Colors.BOLD}Starting MPU6050 simulation on {bridge.port}{Colors.END}")
        click.echo(f"{Colors.INFO}SCL -> PB6, SDA -> PB7.{Colors.END}")
        click.echo(f"{Colors.PROMPT}Press Ctrl+C to exit.{Colors.END}")
        sim.start()
    elif sensor == "bmp280":
        protocol = protocol if protocol else "spi"
        sim = sensor_module.BMP280Simulator(bridge, protocol=protocol)
        click.echo(f"\n{Colors.SUCCESS}{Colors.BOLD}Starting BMP280 simulation on {bridge.port}{Colors.END}")
        if protocol == "spi":
            click.echo(f"{Colors.INFO}SPI: CS->PA4, SCK->PA5, MISO->PA6, MOSI->PA7{Colors.END}")
        else:
            click.echo(f"{Colors.INFO}I2C: SCL->PB6, SDA->PB7 (Address 0x76).{Colors.END}")
        click.echo(f"{Colors.PROMPT}Press Ctrl+C to exit.{Colors.END}")
        sim.start()
    elif sensor == "gps":
        sim = sensor_module.GPSSimulator(bridge)
        click.echo(f"\n{Colors.SUCCESS}{Colors.BOLD}Starting GPS simulation (NMEA UART) on {bridge.port}{Colors.END}")
        click.echo(f"{Colors.INFO}UART6 (9600 Baud): TX->PA11, RX->PA12{Colors.END}")
        click.echo(f"{Colors.PROMPT}Press Ctrl+C to exit.{Colors.END}")
        sim.start()

def main():
    cli(obj={})

if __name__ == "__main__":
    main()
