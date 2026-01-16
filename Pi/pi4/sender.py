# /usr/local/bin/sender.py

import socket
import time

PI5 = "10.10.10.2"
PORT = 6000

# Wait until Ethernet is up
time.sleep(5)

try:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(3)
        s.connect((PI5, PORT))
        s.sendall(b"BOOT\n")
except Exception:
    pass
