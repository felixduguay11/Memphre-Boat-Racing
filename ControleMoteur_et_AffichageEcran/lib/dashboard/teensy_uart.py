import serial
import json

ser = serial.Serial("/dev/serial0",115200,timeout=1)

while True:

    line = ser.readline().decode(errors="ignore").strip()

    if not line:
        continue

    try:
        data = json.loads(line)

        print("ESC",data["id"])
        print("RPM:",data["rpm"])
        print("Voltage:",data["vin"])
        print("Motor Current:",data["motor_current"])
        print()

    except:
        pass