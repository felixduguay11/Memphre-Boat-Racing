import serial

ser = serial.Serial("/dev/serial0",115200)

while True:

    cmd = input("Command: ")

    ser.write((cmd + "\n").encode())