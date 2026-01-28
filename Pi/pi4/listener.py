import socket
import subprocess
import serial
import threading

ETH_IP = "10.10.10.2"
PORT = 6000

# --- UART setup (adjust /dev/ttyS0 or /dev/ttyUSB0 depending on your Pi) ---
ser = serial.Serial("/dev/ttyS0", baudrate=9600, timeout=1)

def handle(cmd):
    cmd = cmd.strip()
    if cmd.startswith("Capture Bee"):
        # Example format: "Capture Bee,HIVE=3,NO=12"
        parts = cmd.split(",")
        hive = 0
        no = 0
        for p in parts:
            if p.startswith("HIVE="):
                hive = int(p.split("=")[1])
            elif p.startswith("NO="):
                no = int(p.split("=")[1])

        # Call capture script with hive and no
        subprocess.run([
            "python3", "/usr/local/bin/Capture_Bee.py",
            str(hive), str(no)
        ])

# --- Thread to read UART and forward to handler ---
def uart_listener():
    while True:
        line = ser.readline().decode().strip()
        if line:
            # Convert "Hive=5 Station=2" into "Capture Bee,HIVE=5,NO=2"
            if line.startswith("Hive="):
                try:
                    parts = line.split()
                    hive = int(parts[0].split("=")[1])
                    no   = int(parts[1].split("=")[1])
                    cmd = f"Capture Bee,HIVE={hive},NO={no}"
                    print(f"UART -> {cmd}")
                    handle(cmd)
                except Exception as e:
                    print("Parse error:", e)

# Start UART listener thread
threading.Thread(target=uart_listener, daemon=True).start()

# --- Socket server ---
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((ETH_IP, PORT))
    s.listen(1)
    print(f"Listening on {ETH_IP}:{PORT} for 'Capture Bee' command...")

    while True:
        conn, _ = s.accept()
        with conn:
            data = conn.recv(1024)
            if data:
                handle(data.decode())
