#!/usr/bin/env python3
"""
MIMIC GUI Interface
===================
A comprehensive graphical interface for the Mimic STM32 Firmware.

Features:
- Auto-detection of serial ports
- Dedicated tabs for GPIO, UART, SPI, and I2C
- Live console log
- Single-file implementation using Tkinter
- Continuous Block Read
- Dynamic Pinout Helper

Dependencies:
    pip install pyserial
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import queue
from datetime import datetime

# =============================================================================
# DATA: Pin Mappings
# =============================================================================
PIN_MAP = {
    'uart': {
        '1': 'TX: PA9, RX: PA10',
        '6': 'TX: PC6, RX: PC7'
    },
    'spi': {
        '1': 'SCK: PA5, MISO: PA6, MOSI: PA7',
        '2': 'SCK: PB13, MISO: PB14, MOSI: PB15',
        '3': 'SCK: PB3, MISO: PB4, MOSI: PB5',
        '4': 'SCK: PE2, MISO: PE5, MOSI: PE6',
        '5': 'SCK: PE12, MISO: PE13, MOSI: PE14'
    },
    'i2c': {
        '1': 'SCL: PB6, SDA: PB7',
        '2': 'SCL: PB10, SDA: PB11',
        '3': 'SCL: PA8, SDA: PC9'
    }
}

# =============================================================================
# BACKEND: Serial Communication Handler
# =============================================================================

class MimicSerial:
    def __init__(self, log_queue):
        self.ser = None
        self.connected = False
        self.log_queue = log_queue
        self.stop_event = threading.Event()
        self.read_thread = None

    def get_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def connect(self, port, baud):
        try:
            self.ser = serial.Serial(port, baud, timeout=0.1)
            self.connected = True
            
            # Start read thread
            self.stop_event.clear()
            self.read_thread = threading.Thread(target=self._read_loop, daemon=True)
            self.read_thread.start()
            
            self._log(f"Connected to {port} at {baud} baud")
            return True
        except serial.SerialException as e:
            self._log(f"Connection Error: {e}")
            return False

    def disconnect(self):
        self.connected = False
        self.stop_event.set()
        if self.ser and self.ser.is_open:
            self.ser.close()
        self._log("Disconnected")

    def send(self, command):
        if not self.connected:
            self._log("Error: Not connected")
            return
        
        try:
            full_cmd = command.strip() + '\r\n'
            self.ser.write(full_cmd.encode())
            self._log(f">> {command}", tag="tx")
        except Exception as e:
            self._log(f"Send Error: {e}", tag="err")

    def _read_loop(self):
        buffer = ""
        while not self.stop_event.is_set() and self.ser and self.ser.is_open:
            try:
                if self.ser.in_waiting:
                    raw = self.ser.read(self.ser.in_waiting)
                    text = raw.decode(errors='ignore')
                    
                    # Split into lines for cleaner logging
                    buffer += text
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line and line != '>' and not line.startswith('>>'):
                            self._log(f"<< {line}", tag="rx")
                            
                time.sleep(0.01)
            except Exception as e:
                self._log(f"Read Error: {e}", tag="err")
                break

    def _log(self, msg, tag="info"):
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log_queue.put((timestamp, tag, msg))

# =============================================================================
# FRONTEND: GUI Application
# =============================================================================

class MimicApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Mimic - Hardware Control Interface")
        self.geometry("950x750")
        
        # Style
        self.style = ttk.Style()
        self.style.theme_use('clam')
        
        # State
        self.log_queue = queue.Queue()
        self.mimic = MimicSerial(self.log_queue)
        self.loops = {'uart': False, 'spi': False, 'i2c': False}
        
        self.create_widgets()
        self.after(100, self.process_log_queue)
        self.after(500, self.auto_connect)  # Try auto-connect after startup

    def auto_connect(self):
        """Attempt to automatically connect to the first available USB port"""
        if not self.mimic.connected:
            ports = self.mimic.get_ports()
            # Prioritize USB ports
            target_port = None
            for p in ports:
                if "USB" in p or "ACM" in p:
                    target_port = p
                    break
            
            # Fallback to first port if no explicit USB port found, but list is not empty
            if not target_port and ports:
                target_port = ports[0]
            
            if target_port:
                self.port_combo.set(target_port)
                self.toggle_connect()
                self.console.config(state='normal')
                self.console.insert(tk.END, f"[System] Auto-connecting to {target_port}...\n", "info")
                self.console.see(tk.END)
                self.console.config(state='disabled')


    def create_widgets(self):
        # --- Top Bar: Connection ---
        conn_frame = ttk.LabelFrame(self, text="Connection", padding=5)
        conn_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Label(conn_frame, text="Port:").pack(side=tk.LEFT, padx=5)
        self.port_combo = ttk.Combobox(conn_frame, width=15)
        self.port_combo.pack(side=tk.LEFT, padx=5)
        self.refresh_ports()
        
        ttk.Button(conn_frame, text="⟳", width=3, command=self.refresh_ports).pack(side=tk.LEFT)
        
        ttk.Label(conn_frame, text="Baud:").pack(side=tk.LEFT, padx=(15, 5))
        self.baud_combo = ttk.Combobox(conn_frame, width=10, values=["9600", "115200", "921600"])
        self.baud_combo.current(1)
        self.baud_combo.pack(side=tk.LEFT, padx=5)
        
        self.btn_connect = ttk.Button(conn_frame, text="Connect", command=self.toggle_connect)
        self.btn_connect.pack(side=tk.LEFT, padx=15)
        
        # --- Main Tabs ---
        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.tab_gpio = self.create_gpio_tab()
        self.tab_uart = self.create_uart_tab()
        self.tab_spi = self.create_spi_tab()
        self.tab_i2c = self.create_i2c_tab()
        self.tab_sys = self.create_sys_tab()
        
        self.notebook.add(self.tab_gpio, text="  GPIO  ")
        self.notebook.add(self.tab_uart, text="  UART  ")
        self.notebook.add(self.tab_spi, text="  SPI   ")
        self.notebook.add(self.tab_i2c, text="  I2C   ")
        self.notebook.add(self.tab_sys, text=" System ")
        
        # --- Bottom Panel: Console ---
        console_frame = ttk.LabelFrame(self, text="Console Log", padding=5)
        console_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.console = scrolledtext.ScrolledText(console_frame, height=10, state='disabled', font=("Consolas", 9))
        self.console.pack(fill=tk.BOTH, expand=True)
        self.console.tag_config("tx", foreground="#0000AA")
        self.console.tag_config("rx", foreground="#006600")
        self.console.tag_config("err", foreground="#AA0000")
        self.console.tag_config("info", foreground="#555555")
        
        # Quick Command Entry
        cmd_frame = ttk.Frame(console_frame)
        cmd_frame.pack(fill=tk.X, pady=(5,0))
        ttk.Label(cmd_frame, text="Cmd:").pack(side=tk.LEFT)
        self.entry_cmd = ttk.Entry(cmd_frame)
        self.entry_cmd.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        self.entry_cmd.bind('<Return>', lambda e: self.send_manual_cmd())
        ttk.Button(cmd_frame, text="Send", command=self.send_manual_cmd).pack(side=tk.LEFT)
        ttk.Button(cmd_frame, text="Clear Log", command=self.clear_log).pack(side=tk.LEFT, padx=5)

    # --- GPIO Tab ---
    def create_gpio_tab(self):
        frame = ttk.Frame(self.notebook)
        
        # --- Pin Selection ---
        sel_frame = ttk.LabelFrame(frame, text="Target Pin", padding=10)
        sel_frame.pack(fill=tk.X, padx=10, pady=10)
        
        ttk.Label(sel_frame, text="Select Pin:").pack(side=tk.LEFT)
        
        pin_options = []
        for port in ['A', 'B', 'C', 'D', 'E']:
            for i in range(16):
                pin_options.append(f"{port}{i}")
        
        self.gpio_pin = ttk.Combobox(sel_frame, width=10, values=pin_options)
        self.gpio_pin.current(0) # Select A0
        self.gpio_pin.pack(side=tk.LEFT, padx=10)
        
        # --- Operations ---
        op_frame = ttk.LabelFrame(frame, text="Pin Operations", padding=10)
        op_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # State Control
        row1 = ttk.Frame(op_frame)
        row1.pack(fill=tk.X, pady=5)
        ttk.Button(row1, text="Set HIGH", width=15,
                   command=lambda: self.mimic.send(f"PIN_HIGH {self.gpio_pin.get()}")).pack(side=tk.LEFT, padx=5)
        ttk.Button(row1, text="Set LOW", width=15,
                   command=lambda: self.mimic.send(f"PIN_LOW {self.gpio_pin.get()}")).pack(side=tk.LEFT, padx=5)
        ttk.Button(row1, text="TOGGLE", width=15,
                   command=lambda: self.mimic.send(f"PIN_TOGGLE {self.gpio_pin.get()}")).pack(side=tk.LEFT, padx=5)

        # Mode Control
        row2 = ttk.Frame(op_frame)
        row2.pack(fill=tk.X, pady=15)
        ttk.Label(row2, text="Configure Mode:").pack(side=tk.LEFT, padx=5)
        ttk.Button(row2, text="OUTPUT", width=10,
                   command=lambda: self.mimic.send(f"PIN_SET_OUT {self.gpio_pin.get()}")).pack(side=tk.LEFT, padx=2)
        ttk.Button(row2, text="INPUT", width=10,
                   command=lambda: self.mimic.send(f"PIN_SET_IN {self.gpio_pin.get()}")).pack(side=tk.LEFT, padx=2)
        ttk.Button(row2, text="INPUT_PULLUP", width=15,
                   command=lambda: self.mimic.send(f"PIN_SET_IN {self.gpio_pin.get()} UP")).pack(side=tk.LEFT, padx=2)
        ttk.Button(row2, text="INPUT_PULLDOWN", width=15,
                   command=lambda: self.mimic.send(f"PIN_SET_IN {self.gpio_pin.get()} DOWN")).pack(side=tk.LEFT, padx=2)

        # Read
        row3 = ttk.Frame(op_frame)
        row3.pack(fill=tk.X, pady=15)
        ttk.Button(row3, text="READ STATE", width=20,
                   command=lambda: self.mimic.send(f"PIN_READ {self.gpio_pin.get()}")).pack(side=tk.LEFT, padx=5)
        ttk.Button(row3, text="GET STATUS (Details)", width=20,
                   command=lambda: self.mimic.send(f"PIN_STATUS {self.gpio_pin.get()}")).pack(side=tk.LEFT, padx=5)

        # Quick Access Grid (LEDs)
        led_frame = ttk.LabelFrame(frame, text="Quick Access (Onboard LEDs)", padding=5)
        led_frame.pack(fill=tk.X, padx=10, pady=10)
        
        leds = [("Green", "D12"), ("Orange", "D13"), ("Red", "D14"), ("Blue", "D15")]
        for label, pin in leds:
            f = ttk.Frame(led_frame)
            f.pack(side=tk.LEFT, expand=True)
            ttk.Label(f, text=label).pack()
            ttk.Button(f, text="Toggle", width=8,
                       command=lambda p=pin: self.mimic.send(f"PIN_TOGGLE {p}")).pack()

        return frame

    # --- UART Tab ---
    def create_uart_tab(self):
        frame = ttk.Frame(self.notebook)
        
        # Setup
        setup_frame = ttk.LabelFrame(frame, text="UART Configuration", padding=10)
        setup_frame.pack(fill=tk.X, padx=10, pady=10)
        
        # Pin Info Update
        def update_pin_info(event=None):
            inst = self.uart_inst.get()
            self.uart_pin_lbl.config(text=f"Pinout: {PIN_MAP['uart'].get(inst, '')}", foreground="blue")

        ttk.Label(setup_frame, text="Instance:").pack(side=tk.LEFT)
        self.uart_inst = ttk.Combobox(setup_frame, width=5, values=["1", "6"])
        self.uart_inst.current(0)
        self.uart_inst.pack(side=tk.LEFT, padx=5)
        self.uart_inst.bind('<<ComboboxSelected>>', update_pin_info)
        
        ttk.Label(setup_frame, text="Baud:").pack(side=tk.LEFT)
        self.uart_baud = ttk.Entry(setup_frame, width=10)
        self.uart_baud.insert(0, "9600")
        self.uart_baud.pack(side=tk.LEFT, padx=5)
        
        ttk.Button(setup_frame, text="Initialize", 
                   command=lambda: self.mimic.send(f"UART_INIT {self.uart_inst.get()} {self.uart_baud.get()}")).pack(side=tk.LEFT, padx=10)
        
        self.uart_pin_lbl = ttk.Label(setup_frame, text="", font=("Arial", 9, "italic"))
        self.uart_pin_lbl.pack(side=tk.LEFT, padx=10)
        update_pin_info() # Init label

        # Transmit
        tx_frame = ttk.LabelFrame(frame, text="Transmit", padding=10)
        tx_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.uart_tx_data = ttk.Entry(tx_frame)
        self.uart_tx_data.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        ttk.Button(tx_frame, text="Send Text", 
                   command=lambda: self.mimic.send(f"UART_SEND {self.uart_inst.get()} {self.uart_tx_data.get()}")).pack(side=tk.LEFT)
        
        # Receive
        rx_frame = ttk.LabelFrame(frame, text="Receive", padding=10)
        rx_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(rx_frame, text="Bytes:").pack(side=tk.LEFT)
        self.uart_rx_len = ttk.Entry(rx_frame, width=5)
        self.uart_rx_len.insert(0, "64")
        self.uart_rx_len.pack(side=tk.LEFT, padx=5)
        
        ttk.Label(rx_frame, text="Timeout (ms):").pack(side=tk.LEFT, padx=(10, 0))
        self.uart_timeout = ttk.Entry(rx_frame, width=8)
        self.uart_timeout.insert(0, "100")
        self.uart_timeout.pack(side=tk.LEFT, padx=5)
        
        ttk.Button(rx_frame, text="Receive Once", 
                   command=lambda: self.mimic.send(f"UART_RECV {self.uart_inst.get()} {self.uart_rx_len.get()} {self.uart_timeout.get()}")).pack(side=tk.LEFT, padx=5)
        
        # Continuous Read
        self.btn_uart_loop = ttk.Button(rx_frame, text="Start Continuous", 
                                        command=lambda: self.toggle_loop('uart'))
        self.btn_uart_loop.pack(side=tk.LEFT, padx=5)
        
        return frame

    # --- SPI Tab ---
    def create_spi_tab(self):
        frame = ttk.Frame(self.notebook)
        
        # Setup
        setup_frame = ttk.LabelFrame(frame, text="SPI Configuration", padding=10)
        setup_frame.pack(fill=tk.X, padx=10, pady=10)
        
        def update_pin_info(event=None):
            inst = self.spi_inst.get()
            self.spi_pin_lbl.config(text=f"Pinout: {PIN_MAP['spi'].get(inst, '')}", foreground="blue")

        grid_opts = {'padx': 5, 'pady': 2}
        
        ttk.Label(setup_frame, text="Instance:").grid(row=0, column=0, sticky='e')
        self.spi_inst = ttk.Combobox(setup_frame, width=5, values=["1", "2", "3", "4", "5"])
        self.spi_inst.current(0)
        self.spi_inst.grid(row=0, column=1, **grid_opts)
        self.spi_inst.bind('<<ComboboxSelected>>', update_pin_info)
        
        ttk.Label(setup_frame, text="Mode:").grid(row=0, column=2, sticky='e')
        self.spi_mode = ttk.Combobox(setup_frame, width=8, values=["MASTER", "SLAVE"])
        self.spi_mode.current(0)
        self.spi_mode.grid(row=0, column=3, **grid_opts)
        
        ttk.Label(setup_frame, text="Speed (Hz):").grid(row=0, column=4, sticky='e')
        self.spi_speed = ttk.Entry(setup_frame, width=10)
        self.spi_speed.insert(0, "1000000")
        self.spi_speed.grid(row=0, column=5, **grid_opts)
        
        ttk.Label(setup_frame, text="CS Pin:").grid(row=0, column=6, sticky='e')
        self.spi_cs = ttk.Entry(setup_frame, width=5)
        self.spi_cs.grid(row=0, column=7, **grid_opts)
        
        ttk.Button(setup_frame, text="Initialize", 
                   command=lambda: self.mimic.send(f"SPI_INIT {self.spi_inst.get()} {self.spi_mode.get()} {self.spi_speed.get()} 0 0 8 MSB {self.spi_cs.get()}")).grid(row=0, column=8, padx=10)
        
        # Pin Label Row
        self.spi_pin_lbl = ttk.Label(setup_frame, text="", font=("Arial", 9, "italic"))
        self.spi_pin_lbl.grid(row=1, column=0, columnspan=9, sticky='w', padx=5, pady=5)
        update_pin_info()

        # Transfer
        trans_frame = ttk.LabelFrame(frame, text="Data Transfer (Hex)", padding=10)
        trans_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.spi_data = ttk.Entry(trans_frame)
        self.spi_data.insert(0, "A5 5A")
        self.spi_data.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        ttk.Button(trans_frame, text="Write Only", 
                   command=lambda: self.mimic.send(f"SPI_SEND {self.spi_inst.get()} {self.spi_data.get()}")).pack(side=tk.LEFT, padx=2)
        ttk.Button(trans_frame, text="Full Duplex", 
                   command=lambda: self.mimic.send(f"SPI_TRANSFER {self.spi_inst.get()} {self.spi_data.get()}")).pack(side=tk.LEFT, padx=2)
                   
        # Receive
        rx_frame = ttk.LabelFrame(frame, text="Receive", padding=10)
        rx_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(rx_frame, text="Bytes:").pack(side=tk.LEFT)
        self.spi_rx_len = ttk.Entry(rx_frame, width=5)
        self.spi_rx_len.insert(0, "4")
        self.spi_rx_len.pack(side=tk.LEFT, padx=5)
        
        ttk.Label(rx_frame, text="Timeout (ms):").pack(side=tk.LEFT, padx=(10, 0))
        self.spi_timeout = ttk.Entry(rx_frame, width=8)
        self.spi_timeout.insert(0, "100")
        self.spi_timeout.pack(side=tk.LEFT, padx=5)
        
        ttk.Button(rx_frame, text="Receive Only", 
                   command=lambda: self.mimic.send(f"SPI_RECV {self.spi_inst.get()} {self.spi_rx_len.get()} {self.spi_timeout.get()}")).pack(side=tk.LEFT, padx=5)
        
         # Continuous Read
        self.btn_spi_loop = ttk.Button(rx_frame, text="Start Continuous", 
                                        command=lambda: self.toggle_loop('spi'))
        self.btn_spi_loop.pack(side=tk.LEFT, padx=5)

        return frame

    # --- I2C Tab ---
    def create_i2c_tab(self):
        frame = ttk.Frame(self.notebook)
        
        # Setup
        setup_frame = ttk.LabelFrame(frame, text="I2C Configuration", padding=10)
        setup_frame.pack(fill=tk.X, padx=10, pady=10)
        
        def update_pin_info(event=None):
            inst = self.i2c_inst.get()
            self.i2c_pin_lbl.config(text=f"Pinout: {PIN_MAP['i2c'].get(inst, '')}", foreground="blue")

        ttk.Label(setup_frame, text="Instance:").grid(row=0, column=0)
        self.i2c_inst = ttk.Combobox(setup_frame, width=5, values=["1", "2", "3"])
        self.i2c_inst.current(0)
        self.i2c_inst.grid(row=0, column=1, padx=5)
        self.i2c_inst.bind('<<ComboboxSelected>>', update_pin_info)
        
        ttk.Label(setup_frame, text="Speed (Hz):").grid(row=0, column=2)
        self.i2c_speed = ttk.Entry(setup_frame, width=10)
        self.i2c_speed.insert(0, "100000")
        self.i2c_speed.grid(row=0, column=3, padx=5)
        
        ttk.Button(setup_frame, text="Initialize Master", 
                   command=lambda: self.mimic.send(f"I2C_INIT {self.i2c_inst.get()} MASTER {self.i2c_speed.get()}")).grid(row=0, column=4, padx=10)
        
        ttk.Button(setup_frame, text="Scan Bus", 
                   command=lambda: self.mimic.send(f"I2C_SCAN {self.i2c_inst.get()}")).grid(row=0, column=5, padx=10)
        
        # Pin Label
        self.i2c_pin_lbl = ttk.Label(setup_frame, text="", font=("Arial", 9, "italic"))
        self.i2c_pin_lbl.grid(row=1, column=0, columnspan=6, sticky='w', padx=5, pady=5)
        update_pin_info()

        # Operations
        op_frame = ttk.LabelFrame(frame, text="Operations", padding=10)
        op_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(op_frame, text="Addr (Hex):").grid(row=0, column=0)
        self.i2c_addr = ttk.Entry(op_frame, width=6)
        self.i2c_addr.insert(0, "0x68")
        self.i2c_addr.grid(row=0, column=1, padx=5)
        
        ttk.Label(op_frame, text="Data (Hex):").grid(row=0, column=2)
        self.i2c_data = ttk.Entry(op_frame, width=15)
        self.i2c_data.insert(0, "75")
        self.i2c_data.grid(row=0, column=3, padx=5)
        
        ttk.Label(op_frame, text="Read Len:").grid(row=0, column=4)
        self.i2c_ulen = ttk.Entry(op_frame, width=5)
        self.i2c_ulen.insert(0, "1")
        self.i2c_ulen.grid(row=0, column=5, padx=5)
        
        ttk.Button(op_frame, text="Write", 
                   command=lambda: self.mimic.send(f"I2C_WRITE {self.i2c_inst.get()} {self.i2c_addr.get()} {self.i2c_data.get()}")).grid(row=1, column=3, pady=5)
        
        ttk.Button(op_frame, text="Read", 
                   command=lambda: self.mimic.send(f"I2C_READ {self.i2c_inst.get()} {self.i2c_addr.get()} {self.i2c_ulen.get()}")).grid(row=1, column=5, pady=5)
        
        ttk.Button(op_frame, text="Write-Read (Reg)", 
                   command=lambda: self.mimic.send(f"I2C_WRITE_READ {self.i2c_inst.get()} {self.i2c_addr.get()} {self.i2c_data.get()} {self.i2c_ulen.get()}")).grid(row=1, column=4, pady=5)

        # Continuous Read
        self.btn_i2c_loop = ttk.Button(op_frame, text="Start Continuous Read", 
                                        command=lambda: self.toggle_loop('i2c'))
        self.btn_i2c_loop.grid(row=2, column=3, columnspan=3, pady=10, sticky="ew")

        return frame

    # --- System Tab ---
    def create_sys_tab(self):
        frame = ttk.Frame(self.notebook)
        
        bg_frame = ttk.Frame(frame, padding=20)
        bg_frame.pack(fill=tk.BOTH, expand=True)
        
        ttk.Button(bg_frame, text="Check Status", 
                   command=lambda: self.mimic.send("STATUS")).pack(pady=5, fill=tk.X)
        ttk.Button(bg_frame, text="Get Version", 
                   command=lambda: self.mimic.send("VERSION")).pack(pady=5, fill=tk.X)
        ttk.Button(bg_frame, text="Help", 
                   command=lambda: self.mimic.send("HELP")).pack(pady=5, fill=tk.X)
                   
        ttk.Separator(bg_frame, orient='horizontal').pack(fill=tk.X, pady=20)
        
        ttk.Button(bg_frame, text="RESET SYSTEM", 
                   command=lambda: self.mimic.send("RESET")).pack(pady=5, fill=tk.X)
                   
        return frame

    # --- Logic ---

    def refresh_ports(self):
        ports = self.mimic.get_ports()
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.current(0)
        else:
            self.port_combo.set("No ports")

    def toggle_connect(self):
        if not self.mimic.connected:
            port = self.port_combo.get()
            baud = self.baud_combo.get()
            if self.mimic.connect(port, int(baud)):
                self.btn_connect.config(text="Disconnect")
                self.port_combo.state(['disabled'])
                self.baud_combo.state(['disabled'])
        else:
            self.mimic.disconnect()
            self.btn_connect.config(text="Connect")
            self.port_combo.state(['!disabled'])
            self.baud_combo.state(['!disabled'])

    def send_manual_cmd(self):
        cmd = self.entry_cmd.get()
        if cmd:
            self.mimic.send(cmd)
            self.entry_cmd.delete(0, tk.END)

    def clear_log(self):
        self.console.config(state='normal')
        self.console.delete(1.0, tk.END)
        self.console.config(state='disabled')

    def process_log_queue(self):
        while not self.log_queue.empty():
            timestamp, tag, msg = self.log_queue.get()
            self.console.config(state='normal')
            self.console.insert(tk.END, f"[{timestamp}] ", "info")
            self.console.insert(tk.END, f"{msg}\n", tag)
            self.console.see(tk.END)
            self.console.config(state='disabled')
        
        self.after(50, self.process_log_queue)

    def toggle_loop(self, type):
        if not self.mimic.connected:
            messagebox.showerror("Error", "Connect to board first!")
            return

        is_running = self.loops.get(type, False)
        
        if is_running:
            # STOP
            self.loops[type] = False
            if type == 'uart': self.btn_uart_loop.config(text="Start Continuous")
            if type == 'spi': self.btn_spi_loop.config(text="Start Continuous")
            if type == 'i2c': self.btn_i2c_loop.config(text="Start Continuous Read")
        else:
            # START
            self.loops[type] = True
            if type == 'uart': self.btn_uart_loop.config(text="STOP Continuous")
            if type == 'spi': self.btn_spi_loop.config(text="STOP Continuous")
            if type == 'i2c': self.btn_i2c_loop.config(text="STOP Continuous Read")
            
            self.run_loop_cycle(type)

    def run_loop_cycle(self, type):
        if not self.loops.get(type, False) or not self.mimic.connected:
            return

        cmd = ""
        if type == 'uart':
            cmd = f"UART_RECV {self.uart_inst.get()} {self.uart_rx_len.get()} {self.uart_timeout.get()}"
        elif type == 'spi':
            cmd = f"SPI_RECV {self.spi_inst.get()} {self.spi_rx_len.get()} {self.spi_timeout.get()}"
        elif type == 'i2c':
            cmd = f"I2C_READ {self.i2c_inst.get()} {self.i2c_addr.get()} {self.i2c_ulen.get()}"
            
        if cmd:
            self.mimic.send(cmd)
        
        # Schedule next iteration (100ms delay)
        self.after(100, lambda: self.run_loop_cycle(type))

if __name__ == "__main__":
    app = MimicApp()
    app.mainloop()
