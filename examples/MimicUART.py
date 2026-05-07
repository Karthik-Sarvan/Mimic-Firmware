#!/usr/bin/env python3
"""
Mimic - Interactive Protocol Testing Tool for STM32
====================================================

A single interactive tool that:
1. Takes user commands for protocol configuration
2. Auto-generates STM32 firmware code
3. Compiles and flashes automatically
4. Shows live serial monitor with TX/RX data

Usage:
    python MimicUART.py

Author: Mimic Project
"""

import os
import sys
import time
import threading
import subprocess
import signal
import readline
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional, List, Callable
from datetime import datetime

# Try to import serial, install if not available
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Installing pyserial...")
    subprocess.run([sys.executable, "-m", "pip", "install", "pyserial", "-q"])
    import serial
    import serial.tools.list_ports


# =============================================================================
# CONFIGURATION
# =============================================================================

WORKSPACE_DIR = os.path.dirname(os.path.abspath(__file__))
FIRMWARE_DIR = os.path.join(WORKSPACE_DIR, "Mimic")
CORE_SRC_DIR = os.path.join(FIRMWARE_DIR, "Core", "Src")
CORE_INC_DIR = os.path.join(FIRMWARE_DIR, "Core", "Inc")


class Parity(Enum):
    NONE = "N"
    EVEN = "E"
    ODD = "O"


class FrameMode(Enum):
    CONTINUOUS = "continuous"  # Send/receive continuously
    ECHO = "echo"              # Echo back received data
    COMMAND = "command"        # Wait for command, respond
    ONESHOT = "oneshot"        # Send once


@dataclass
class UARTConfig:
    """UART Configuration"""
    instance: str = "USART2"
    baud_rate: int = 9600
    data_bits: int = 8
    parity: Parity = Parity.NONE
    stop_bits: int = 1
    
    # What to do
    mode: FrameMode = FrameMode.ECHO
    tx_data: str = ""
    tx_interval_ms: int = 1000
    tx_hex: bool = False  # If True, tx_data is hex string
    
    def get_config_string(self) -> str:
        return f"{self.baud_rate} {self.data_bits}{self.parity.value}{self.stop_bits}"


# =============================================================================
# COLORS FOR TERMINAL
# =============================================================================

class Colors:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    MAGENTA = "\033[95m"
    CYAN = "\033[96m"
    WHITE = "\033[97m"
    
    BG_RED = "\033[41m"
    BG_GREEN = "\033[42m"
    BG_BLUE = "\033[44m"
    
    @staticmethod
    def tx(text):
        return f"{Colors.GREEN}[TX] {text}{Colors.RESET}"
    
    @staticmethod
    def rx(text):
        return f"{Colors.CYAN}[RX] {text}{Colors.RESET}"
    
    @staticmethod
    def info(text):
        return f"{Colors.BLUE}[INFO] {text}{Colors.RESET}"
    
    @staticmethod
    def error(text):
        return f"{Colors.RED}[ERROR] {text}{Colors.RESET}"
    
    @staticmethod
    def success(text):
        return f"{Colors.GREEN}[✓] {text}{Colors.RESET}"
    
    @staticmethod
    def warning(text):
        return f"{Colors.YELLOW}[!] {text}{Colors.RESET}"


# =============================================================================
# FIRMWARE CODE GENERATOR
# =============================================================================

class FirmwareGenerator:
    """Generates STM32 firmware based on configuration"""
    
    def __init__(self, config: UARTConfig):
        self.config = config
        self.handle = f"h{config.instance.lower()}"
    
    def _get_hal_parity(self) -> str:
        mapping = {
            Parity.NONE: "UART_PARITY_NONE",
            Parity.EVEN: "UART_PARITY_EVEN",
            Parity.ODD: "UART_PARITY_ODD"
        }
        return mapping[self.config.parity]
    
    def _get_hal_stopbits(self) -> str:
        return "UART_STOPBITS_2" if self.config.stop_bits == 2 else "UART_STOPBITS_1"
    
    def _get_word_length(self) -> str:
        if self.config.parity != Parity.NONE:
            return "UART_WORDLENGTH_9B"
        return "UART_WORDLENGTH_8B"
    
    def _escape_string(self, s: str) -> str:
        """Escape string for C code"""
        return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r')
    
    def _hex_to_bytes(self, hex_str: str) -> List[int]:
        """Convert hex string to byte list"""
        hex_str = hex_str.replace(" ", "").replace("0x", "")
        return [int(hex_str[i:i+2], 16) for i in range(0, len(hex_str), 2)]
    
    def generate_main_c(self) -> str:
        """Generate the main.c file"""
        cfg = self.config
        handle = self.handle
        
        # Prepare TX data
        if cfg.tx_hex and cfg.tx_data:
            tx_bytes = self._hex_to_bytes(cfg.tx_data)
            tx_data_def = "uint8_t tx_data[] = {" + ", ".join(f"0x{b:02X}" for b in tx_bytes) + "};"
            tx_len = len(tx_bytes)
        elif cfg.tx_data:
            escaped = self._escape_string(cfg.tx_data)
            tx_data_def = f'uint8_t tx_data[] = "{escaped}";'
            tx_len = len(cfg.tx_data)
        else:
            tx_data_def = 'uint8_t tx_data[] = "";'
            tx_len = 0
        
        code = f'''/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body - Auto-generated by Mimic
  * @config         : {cfg.get_config_string()} | Mode: {cfg.mode.value}
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include <string.h>

/* Private variables ---------------------------------------------------------*/
{tx_data_def}
uint8_t rx_buffer[256];
uint8_t rx_byte;
uint16_t rx_index = 0;
uint32_t last_tx_time = 0;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_{cfg.instance}_UART_Init();
    
'''
        # Mode-specific main loop
        if cfg.mode == FrameMode.ECHO:
            code += f'''    /* ECHO MODE: Receive and echo back */
    while (1)
    {{
        if (HAL_UART_Receive(&{handle}, &rx_byte, 1, 10) == HAL_OK)
        {{
            /* Echo back immediately */
            HAL_UART_Transmit(&{handle}, &rx_byte, 1, 10);
            
            /* Also store in buffer */
            if (rx_index < 255)
            {{
                rx_buffer[rx_index++] = rx_byte;
            }}
            
            /* If newline, also send the full line */
            if (rx_byte == '\\n' || rx_byte == '\\r')
            {{
                rx_buffer[rx_index] = '\\0';
                rx_index = 0;
            }}
        }}
    }}
'''
        elif cfg.mode == FrameMode.CONTINUOUS:
            code += f'''    /* CONTINUOUS MODE: Send data every {cfg.tx_interval_ms}ms, receive in between */
    while (1)
    {{
        /* Send data periodically */
        if (HAL_GetTick() - last_tx_time >= {cfg.tx_interval_ms})
        {{
            HAL_UART_Transmit(&{handle}, tx_data, {tx_len}, 100);
            last_tx_time = HAL_GetTick();
        }}
        
        /* Check for received data */
        if (HAL_UART_Receive(&{handle}, &rx_byte, 1, 1) == HAL_OK)
        {{
            /* Echo back */
            HAL_UART_Transmit(&{handle}, &rx_byte, 1, 10);
        }}
    }}
'''
        elif cfg.mode == FrameMode.ONESHOT:
            code += f'''    /* ONESHOT MODE: Send once, then echo */
    HAL_UART_Transmit(&{handle}, tx_data, {tx_len}, 1000);
    
    while (1)
    {{
        if (HAL_UART_Receive(&{handle}, &rx_byte, 1, 10) == HAL_OK)
        {{
            HAL_UART_Transmit(&{handle}, &rx_byte, 1, 10);
        }}
    }}
'''
        elif cfg.mode == FrameMode.COMMAND:
            code += f'''    /* COMMAND MODE: Wait for commands, respond */
    uint8_t cmd_buffer[64];
    uint8_t cmd_idx = 0;
    
    /* Send ready message */
    HAL_UART_Transmit(&{handle}, (uint8_t*)"READY\\r\\n", 7, 100);
    
    while (1)
    {{
        if (HAL_UART_Receive(&{handle}, &rx_byte, 1, 10) == HAL_OK)
        {{
            /* Echo character */
            HAL_UART_Transmit(&{handle}, &rx_byte, 1, 10);
            
            if (rx_byte == '\\n' || rx_byte == '\\r')
            {{
                cmd_buffer[cmd_idx] = '\\0';
                
                /* Process command */
                if (strcmp((char*)cmd_buffer, "PING") == 0)
                {{
                    HAL_UART_Transmit(&{handle}, (uint8_t*)"PONG\\r\\n", 6, 100);
                }}
                else if (strcmp((char*)cmd_buffer, "STATUS") == 0)
                {{
                    HAL_UART_Transmit(&{handle}, (uint8_t*)"OK\\r\\n", 4, 100);
                }}
                else if (strcmp((char*)cmd_buffer, "LED ON") == 0)
                {{
                    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
                    HAL_UART_Transmit(&{handle}, (uint8_t*)"LED ON\\r\\n", 8, 100);
                }}
                else if (strcmp((char*)cmd_buffer, "LED OFF") == 0)
                {{
                    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET);
                    HAL_UART_Transmit(&{handle}, (uint8_t*)"LED OFF\\r\\n", 9, 100);
                }}
                else if (cmd_idx > 0)
                {{
                    HAL_UART_Transmit(&{handle}, (uint8_t*)"UNKNOWN: ", 9, 100);
                    HAL_UART_Transmit(&{handle}, cmd_buffer, cmd_idx, 100);
                    HAL_UART_Transmit(&{handle}, (uint8_t*)"\\r\\n", 2, 100);
                }}
                
                cmd_idx = 0;
            }}
            else if (cmd_idx < 63)
            {{
                cmd_buffer[cmd_idx++] = rx_byte;
            }}
        }}
    }}
'''
        
        # System clock config (same as before)
        code += '''
    return 0;
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 50;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 8;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
'''
        return code
    
    def generate_usart_c(self) -> str:
        """Generate usart.c with current UART config"""
        cfg = self.config
        handle = self.handle
        
        return f'''/* USART Configuration - Auto-generated by Mimic */
#include "usart.h"

UART_HandleTypeDef {handle};

void MX_{cfg.instance}_UART_Init(void)
{{
    {handle}.Instance = {cfg.instance};
    {handle}.Init.BaudRate = {cfg.baud_rate};
    {handle}.Init.WordLength = {self._get_word_length()};
    {handle}.Init.StopBits = {self._get_hal_stopbits()};
    {handle}.Init.Parity = {self._get_hal_parity()};
    {handle}.Init.Mode = UART_MODE_TX_RX;
    {handle}.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    {handle}.Init.OverSampling = UART_OVERSAMPLING_16;
    
    if (HAL_UART_Init(&{handle}) != HAL_OK)
    {{
        Error_Handler();
    }}
}}
'''
    
    def generate_usart_h(self) -> str:
        """Generate usart.h header"""
        cfg = self.config
        handle = self.handle
        
        return f'''/* USART Header - Auto-generated by Mimic */
#ifndef __USART_H__
#define __USART_H__

#include "main.h"

extern UART_HandleTypeDef {handle};

void MX_{cfg.instance}_UART_Init(void);

#endif /* __USART_H__ */
'''
    
    def save_files(self):
        """Save generated files to firmware directory"""
        # Save main.c
        main_path = os.path.join(CORE_SRC_DIR, "main.c")
        with open(main_path, 'w') as f:
            f.write(self.generate_main_c())
        
        # Save usart.c
        usart_c_path = os.path.join(CORE_SRC_DIR, "usart.c")
        with open(usart_c_path, 'w') as f:
            f.write(self.generate_usart_c())
        
        # Save usart.h
        usart_h_path = os.path.join(CORE_INC_DIR, "usart.h")
        with open(usart_h_path, 'w') as f:
            f.write(self.generate_usart_h())


# =============================================================================
# SERIAL MONITOR
# =============================================================================

class SerialMonitor:
    """Handles serial port communication and display"""
    
    def __init__(self, port: str, baud_rate: int):
        self.port = port
        self.baud_rate = baud_rate
        self.serial: Optional[serial.Serial] = None
        self.running = False
        self.rx_thread: Optional[threading.Thread] = None
        self.rx_callback: Optional[Callable] = None
        self.rx_buffer = bytearray()
    
    def find_ttl_port(self) -> Optional[str]:
        """Find USB-TTL converter port"""
        ports = serial.tools.list_ports.comports()
        for port in ports:
            # Common USB-TTL chips
            if any(x in port.description.lower() for x in ['ch340', 'cp210', 'ftdi', 'pl2303', 'usb']):
                if 'ttyUSB' in port.device or 'ttyACM' in port.device:
                    return port.device
        # Default fallback
        for port in ports:
            if 'ttyUSB' in port.device:
                return port.device
        return None
    
    def connect(self) -> bool:
        """Connect to serial port"""
        try:
            if self.serial and self.serial.is_open:
                self.serial.close()
            
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baud_rate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1
            )
            return True
        except Exception as e:
            print(Colors.error(f"Failed to open {self.port}: {e}"))
            return False
    
    def disconnect(self):
        """Disconnect from serial port"""
        self.running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=1)
        if self.serial and self.serial.is_open:
            self.serial.close()
    
    def start_monitoring(self, callback: Callable):
        """Start background thread to monitor RX"""
        self.rx_callback = callback
        self.running = True
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()
    
    def _rx_loop(self):
        """Background loop to read serial data"""
        while self.running:
            try:
                if self.serial and self.serial.is_open and self.serial.in_waiting:
                    data = self.serial.read(self.serial.in_waiting)
                    if data and self.rx_callback:
                        self.rx_callback(data)
                else:
                    time.sleep(0.01)
            except Exception as e:
                if self.running:
                    print(Colors.error(f"RX Error: {e}"))
                time.sleep(0.1)
    
    def send(self, data: bytes) -> bool:
        """Send data via serial"""
        try:
            if self.serial and self.serial.is_open:
                self.serial.write(data)
                return True
        except Exception as e:
            print(Colors.error(f"TX Error: {e}"))
        return False
    
    def send_string(self, text: str) -> bool:
        """Send string via serial"""
        return self.send(text.encode('utf-8'))


# =============================================================================
# MIMIC - MAIN APPLICATION
# =============================================================================

class MimicApp:
    """Main Mimic Application"""
    
    def __init__(self):
        self.config = UARTConfig()
        self.serial_monitor: Optional[SerialMonitor] = None
        self.running = True
        self.monitor_active = False
        
        # Command history
        self.history_file = os.path.expanduser("~/.mimic_history")
        self._load_history()
        
        # Find TTL port
        self.ttl_port = self._find_ttl_port()
    
    def _find_ttl_port(self) -> str:
        """Find USB-TTL port"""
        ports = serial.tools.list_ports.comports()
        for port in ports:
            if 'ttyUSB' in port.device:
                return port.device
        return "/dev/ttyUSB0"
    
    def _load_history(self):
        """Load command history"""
        try:
            readline.read_history_file(self.history_file)
        except FileNotFoundError:
            pass
    
    def _save_history(self):
        """Save command history"""
        try:
            readline.write_history_file(self.history_file)
        except:
            pass
    
    def print_banner(self):
        """Print welcome banner"""
        print(f"""
{Colors.CYAN}{Colors.BOLD}
╔═══════════════════════════════════════════════════════════════════════╗
║                                                                       ║
║   ███╗   ███╗██╗███╗   ███╗██╗ ██████╗                               ║
║   ████╗ ████║██║████╗ ████║██║██╔════╝                               ║
║   ██╔████╔██║██║██╔████╔██║██║██║                                    ║
║   ██║╚██╔╝██║██║██║╚██╔╝██║██║██║                                    ║
║   ██║ ╚═╝ ██║██║██║ ╚═╝ ██║██║╚██████╗                               ║
║   ╚═╝     ╚═╝╚═╝╚═╝     ╚═╝╚═╝ ╚═════╝                               ║
║                                                                       ║
║   Interactive Protocol Testing Tool for STM32                         ║
║   Auto-generates firmware, compiles, flashes, and monitors            ║
╚═══════════════════════════════════════════════════════════════════════╝
{Colors.RESET}""")
        print(f"  {Colors.DIM}TTL Port: {self.ttl_port}{Colors.RESET}")
        print(f"  {Colors.DIM}Type 'help' for commands{Colors.RESET}\n")
    
    def print_help(self):
        """Print help message"""
        print(f"""
{Colors.BOLD}UART COMMANDS:{Colors.RESET}
  {Colors.GREEN}uart config <baud> [8N1|8E1|8O1|etc]{Colors.RESET}
      Configure UART settings
      Example: uart config 115200 8N1

  {Colors.GREEN}uart send "<text>"{Colors.RESET}
      Generate firmware that sends text continuously
      Example: uart send "Hello World\\n"

  {Colors.GREEN}uart send hex <bytes>{Colors.RESET}
      Send hex data
      Example: uart send hex 48 45 4C 4C 4F

  {Colors.GREEN}uart echo{Colors.RESET}
      Generate firmware that echoes received data

  {Colors.GREEN}uart command{Colors.RESET}
      Generate firmware that responds to commands (PING, STATUS, LED ON/OFF)

  {Colors.GREEN}uart interval <ms>{Colors.RESET}
      Set TX interval for continuous mode
      Example: uart interval 500

{Colors.BOLD}MONITOR COMMANDS:{Colors.RESET}
  {Colors.GREEN}monitor [start|stop]{Colors.RESET}
      Start/stop serial monitor

  {Colors.GREEN}send "<text>"{Colors.RESET}
      Send text via TTL (when monitor is active)

  {Colors.GREEN}send hex <bytes>{Colors.RESET}
      Send hex bytes via TTL

{Colors.BOLD}BUILD COMMANDS:{Colors.RESET}
  {Colors.GREEN}build{Colors.RESET}
      Generate code and compile firmware

  {Colors.GREEN}flash{Colors.RESET}
      Flash firmware to STM32

  {Colors.GREEN}run{Colors.RESET}
      Build, flash, and start monitor (all-in-one)

{Colors.BOLD}OTHER COMMANDS:{Colors.RESET}
  {Colors.GREEN}status{Colors.RESET}
      Show current configuration

  {Colors.GREEN}ports{Colors.RESET}
      List available serial ports

  {Colors.GREEN}port <device>{Colors.RESET}
      Set TTL port (e.g., port /dev/ttyUSB0)

  {Colors.GREEN}clear{Colors.RESET}
      Clear screen

  {Colors.GREEN}help{Colors.RESET}
      Show this help

  {Colors.GREEN}exit | quit{Colors.RESET}
      Exit Mimic
""")
    
    def print_status(self):
        """Print current configuration status"""
        cfg = self.config
        print(f"""
{Colors.BOLD}Current Configuration:{Colors.RESET}
  UART Instance:  {Colors.CYAN}{cfg.instance}{Colors.RESET}
  Baud Rate:      {Colors.CYAN}{cfg.baud_rate}{Colors.RESET}
  Format:         {Colors.CYAN}{cfg.data_bits}{cfg.parity.value}{cfg.stop_bits}{Colors.RESET}
  Mode:           {Colors.CYAN}{cfg.mode.value}{Colors.RESET}
  TX Data:        {Colors.CYAN}{repr(cfg.tx_data) if cfg.tx_data else '(none)'}{Colors.RESET}
  TX Interval:    {Colors.CYAN}{cfg.tx_interval_ms} ms{Colors.RESET}
  TX Hex Mode:    {Colors.CYAN}{cfg.tx_hex}{Colors.RESET}
  
  TTL Port:       {Colors.YELLOW}{self.ttl_port}{Colors.RESET}
  Monitor Active: {Colors.GREEN if self.monitor_active else Colors.RED}{self.monitor_active}{Colors.RESET}
""")
    
    def list_ports(self):
        """List available serial ports"""
        ports = serial.tools.list_ports.comports()
        print(f"\n{Colors.BOLD}Available Serial Ports:{Colors.RESET}")
        if not ports:
            print("  No ports found")
        for port in ports:
            marker = " <-- TTL" if port.device == self.ttl_port else ""
            print(f"  {Colors.CYAN}{port.device}{Colors.RESET}: {port.description}{Colors.GREEN}{marker}{Colors.RESET}")
        print()
    
    def parse_uart_config(self, format_str: str):
        """Parse format string like 8N1, 8E1, 8O2"""
        format_str = format_str.upper()
        if len(format_str) >= 3:
            self.config.data_bits = int(format_str[0])
            parity_char = format_str[1]
            self.config.parity = {'N': Parity.NONE, 'E': Parity.EVEN, 'O': Parity.ODD}.get(parity_char, Parity.NONE)
            self.config.stop_bits = int(format_str[2])
    
    def generate_and_save(self):
        """Generate firmware code and save to files"""
        print(Colors.info("Generating firmware code..."))
        generator = FirmwareGenerator(self.config)
        generator.save_files()
        print(Colors.success(f"Generated code for {self.config.mode.value} mode"))
    
    def build_firmware(self) -> bool:
        """Compile the firmware"""
        print(Colors.info("Compiling firmware..."))
        result = subprocess.run(
            ["make", "-C", FIRMWARE_DIR, "clean"],
            capture_output=True, text=True
        )
        result = subprocess.run(
            ["make", "-C", FIRMWARE_DIR],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            print(Colors.success("Build successful"))
            return True
        else:
            print(Colors.error("Build failed:"))
            print(result.stderr)
            return False
    
    def flash_firmware(self) -> bool:
        """Flash firmware to STM32"""
        print(Colors.info("Flashing firmware..."))
        result = subprocess.run(
            ["make", "-C", FIRMWARE_DIR, "flash"],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            print(Colors.success("Flash successful"))
            return True
        else:
            print(Colors.error("Flash failed:"))
            print(result.stderr)
            return False
    
    def start_monitor(self):
        """Start serial monitor"""
        if self.monitor_active:
            print(Colors.warning("Monitor already active"))
            return
        
        self.serial_monitor = SerialMonitor(self.ttl_port, self.config.baud_rate)
        if self.serial_monitor.connect():
            self.serial_monitor.start_monitoring(self._on_rx_data)
            self.monitor_active = True
            print(Colors.success(f"Monitor started on {self.ttl_port} @ {self.config.baud_rate}"))
            print(Colors.info("Type messages to send, or commands like 'monitor stop'"))
        else:
            self.monitor_active = False
    
    def stop_monitor(self):
        """Stop serial monitor"""
        if self.serial_monitor:
            self.serial_monitor.disconnect()
            self.serial_monitor = None
        self.monitor_active = False
        print(Colors.info("Monitor stopped"))
    
    def _on_rx_data(self, data: bytes):
        """Callback for received data"""
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        try:
            text = data.decode('utf-8', errors='replace')
            # Print with RX prefix
            for line in text.split('\n'):
                if line.strip():
                    print(f"\r{Colors.CYAN}[RX {timestamp}] {line}{Colors.RESET}")
            # Reprint prompt
            if self.monitor_active:
                print(f"{Colors.GREEN}mimic[monitor]{Colors.RESET}> ", end='', flush=True)
        except:
            # Print as hex
            hex_str = ' '.join(f'{b:02X}' for b in data)
            print(f"\r{Colors.CYAN}[RX {timestamp}] {hex_str}{Colors.RESET}")
            if self.monitor_active:
                print(f"{Colors.GREEN}mimic[monitor]{Colors.RESET}> ", end='', flush=True)
    
    def send_data(self, text: str):
        """Send data via monitor"""
        if not self.monitor_active or not self.serial_monitor:
            print(Colors.error("Monitor not active. Use 'monitor start' first"))
            return
        
        # Add newline if not present
        if not text.endswith('\n') and not text.endswith('\r'):
            text += '\r\n'
        
        if self.serial_monitor.send_string(text):
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(f"{Colors.GREEN}[TX {timestamp}] {repr(text)}{Colors.RESET}")
    
    def send_hex(self, hex_bytes: List[int]):
        """Send hex bytes via monitor"""
        if not self.monitor_active or not self.serial_monitor:
            print(Colors.error("Monitor not active. Use 'monitor start' first"))
            return
        
        data = bytes(hex_bytes)
        if self.serial_monitor.send(data):
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            hex_str = ' '.join(f'{b:02X}' for b in data)
            print(f"{Colors.GREEN}[TX {timestamp}] {hex_str}{Colors.RESET}")
    
    def process_command(self, cmd: str):
        """Process user command"""
        cmd = cmd.strip()
        if not cmd:
            return
        
        parts = cmd.split()
        action = parts[0].lower()
        
        try:
            # UART commands
            if action == "uart":
                if len(parts) < 2:
                    print("Usage: uart <config|send|echo|command|interval> ...")
                    return
                
                sub = parts[1].lower()
                
                if sub == "config":
                    if len(parts) >= 3:
                        self.config.baud_rate = int(parts[2])
                    if len(parts) >= 4:
                        self.parse_uart_config(parts[3])
                    print(Colors.success(f"UART configured: {self.config.get_config_string()}"))
                
                elif sub == "send":
                    if len(parts) >= 3:
                        if parts[2].lower() == "hex":
                            # Hex mode
                            hex_bytes = [int(x, 16) for x in parts[3:]]
                            self.config.tx_data = ' '.join(parts[3:])
                            self.config.tx_hex = True
                        else:
                            # Text mode - extract quoted string
                            text = ' '.join(parts[2:])
                            if text.startswith('"') and text.endswith('"'):
                                text = text[1:-1]
                            # Handle escape sequences
                            text = text.replace('\\n', '\n').replace('\\r', '\r')
                            self.config.tx_data = text
                            self.config.tx_hex = False
                        self.config.mode = FrameMode.CONTINUOUS
                        print(Colors.success(f"TX data set: {repr(self.config.tx_data)}"))
                
                elif sub == "echo":
                    self.config.mode = FrameMode.ECHO
                    print(Colors.success("Mode set to ECHO"))
                
                elif sub == "command":
                    self.config.mode = FrameMode.COMMAND
                    print(Colors.success("Mode set to COMMAND"))
                
                elif sub == "interval":
                    if len(parts) >= 3:
                        self.config.tx_interval_ms = int(parts[2])
                        print(Colors.success(f"TX interval set to {self.config.tx_interval_ms} ms"))
            
            # Build commands
            elif action == "build":
                self.generate_and_save()
                self.build_firmware()
            
            elif action == "flash":
                self.flash_firmware()
            
            elif action == "run":
                self.generate_and_save()
                if self.build_firmware():
                    if self.flash_firmware():
                        time.sleep(0.5)  # Wait for board to reset
                        self.start_monitor()
            
            # Monitor commands
            elif action == "monitor":
                if len(parts) >= 2:
                    if parts[1].lower() == "start":
                        self.start_monitor()
                    elif parts[1].lower() == "stop":
                        self.stop_monitor()
                else:
                    if self.monitor_active:
                        self.stop_monitor()
                    else:
                        self.start_monitor()
            
            elif action == "send":
                if len(parts) >= 2:
                    if parts[1].lower() == "hex":
                        hex_bytes = [int(x, 16) for x in parts[2:]]
                        self.send_hex(hex_bytes)
                    else:
                        text = ' '.join(parts[1:])
                        if text.startswith('"') and text.endswith('"'):
                            text = text[1:-1]
                        text = text.replace('\\n', '\n').replace('\\r', '\r')
                        self.send_data(text)
            
            # If monitor is active and not a known command, send as message
            elif self.monitor_active and action not in ['status', 'ports', 'port', 'clear', 'help', 'exit', 'quit', 'q']:
                self.send_data(cmd)
                return
            
            # Other commands
            elif action == "status":
                self.print_status()
            
            elif action == "ports":
                self.list_ports()
            
            elif action == "port":
                if len(parts) >= 2:
                    self.ttl_port = parts[1]
                    print(Colors.success(f"TTL port set to {self.ttl_port}"))
                    if self.monitor_active:
                        self.stop_monitor()
                        self.start_monitor()
            
            elif action == "clear":
                os.system('clear')
            
            elif action == "help":
                self.print_help()
            
            elif action in ["exit", "quit", "q"]:
                self.running = False
            
            else:
                print(Colors.warning(f"Unknown command: {action}. Type 'help' for commands."))
        
        except Exception as e:
            print(Colors.error(f"Error: {e}"))
    
    def run(self):
        """Main run loop"""
        self.print_banner()
        
        # Handle Ctrl+C gracefully
        def signal_handler(sig, frame):
            if self.monitor_active:
                print("\n")
            else:
                print("\n" + Colors.info("Use 'exit' to quit"))
        
        signal.signal(signal.SIGINT, signal_handler)
        
        while self.running:
            try:
                # Show different prompt based on monitor state
                if self.monitor_active:
                    prompt = f"{Colors.GREEN}mimic[monitor]{Colors.RESET}> "
                else:
                    prompt = f"{Colors.BLUE}mimic{Colors.RESET}> "
                
                cmd = input(prompt)
                self.process_command(cmd)
                
            except EOFError:
                break
            except KeyboardInterrupt:
                print()
                continue
        
        # Cleanup
        self.stop_monitor()
        self._save_history()
        print(Colors.info("Goodbye!"))


# =============================================================================
# MAIN ENTRY POINT
# =============================================================================

if __name__ == "__main__":
    app = MimicApp()
    app.run()
