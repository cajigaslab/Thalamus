import serial

ser = serial.Serial('/dev/cu.usbmodem31101', 115200, timeout=1)

while True:
    if ser.in_waiting > 0:
        try:
            data = ser.readline().decode('utf-8').strip()
            print(f"Analog Value: {data}")
        except UnicodeDecodeError:
            print("Error decoding data")