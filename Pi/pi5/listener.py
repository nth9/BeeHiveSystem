import socket
import subprocess

ETH_IP = "10.10.10.2"
PORT = 6000

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
            "python3", "/usr/local/bin/cam_capture_bee.py",
            str(hive), str(no)
        ])

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
