# /usr/local/bin/listener.py

import socket
import subprocess

ETH_IP = "10.10.10.2"
PORT = 6000

def handle(cmd):
    cmd = cmd.strip()
    if cmd == "BOOT":
        subprocess.run(["systemctl", "start", "target.service"])

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((ETH_IP, PORT))
    s.listen(1)

    while True:
        conn, _ = s.accept()
        with conn:
            data = conn.recv(1024)
            if data:
                handle(data.decode())
