from mimic import MimicBridge
import time

bridge = MimicBridge()
if bridge.connect():
    # 1. Setup pin B0 as input with pull-up
    bridge.execute("PIN_SET_IN B0 UP")
    
    print("Waiting for button press (B0 to go LOW)...")
    
    # 2. Polling loop to wait for state change
    while True:
        response = bridge.execute("PIN_READ B0")
        if any("LOW" in r for r in response):
            print("Button Pressed!")
            break
        time.sleep(0.01) # Small delay to avoid flooding the CPU
