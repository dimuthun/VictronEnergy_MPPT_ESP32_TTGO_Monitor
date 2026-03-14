#!/usr/bin/env python3
"""Capture ESP32 Serial output to debug.log. Run: python serial_to_debug_log.py COM3"""
import sys
import serial
import json

LOG_PATH = __file__.replace("serial_to_debug_log.py", "debug.log")
BAUD = 115200

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    try:
        ser = serial.Serial(port, BAUD, timeout=0.5)
    except Exception as e:
        print(f"Open {port} failed: {e}")
        sys.exit(1)
    with open(LOG_PATH, "a", encoding="utf-8") as f:
        while True:
            try:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if line and line.startswith("{"):
                    try:
                        json.loads(line)
                        f.write(line + "\n")
                    except json.JSONDecodeError:
                        f.write(json.dumps({"raw": line}) + "\n")
                    f.flush()
                    print(line)
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Err: {e}")
    ser.close()

if __name__ == "__main__":
    main()
