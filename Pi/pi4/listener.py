import socket
import subprocess
import os
import time

PI4_CMD_PORT = 5000
PI5_IP = "192.168.1.20"   # Pi5 LAN IP
PI5_IMG_PORT = 5001       # Pi5 image receiver port

def preprocess_image(filename):
    # Optional: run any preprocessing before sending
    print(f"Preprocessing {filename}...")
    return filename

print(f"Pi4 listening on port {PI4_CMD_PORT}...")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind(("", PI4_CMD_PORT))
    s.listen()
    while True:
        conn, addr = s.accept()
        with conn:
            print(f"Command connection from {addr}")
            data = conn.recv(1024).decode("utf-8", errors="ignore").strip()
            if data:
                print(f"LAN received: {data}")
                line = line.strip()
                if line.startswith("Front"):
                    parts = data.split(",")
                    hive = int(parts[0].split("=")[1])
                    no = int(parts[1].split("=")[1])
                    
                    # Run Pi5 capture script (Front)
                    script_path = os.path.join(os.path.dirname(__file__), "Capture_Bee_Front.py")
                    subprocess.run(["python3", script_path, str(hive), str(no)])

                    # Step 3: pause before connecting back
                    time.sleep(3)

                    # Step 3: connect back to Pi5 on port 5001 and send
                    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s_img:
                        s_img.connect((PI5_IP, PI5_IMG_PORT))
                        header = f"HIVE={hive},NO={no},FILENAME={filename}\n"
                        s_img.sendall(header.encode("utf-8"))
                        with open(filename, "rb") as f:
                            while True:
                                chunk = f.read(4096)
                                if not chunk:
                                    break
                                s_img.sendall(chunk)
                        print(f"Image {filename} sent to Pi5")
                else if line.startswith("Back"):
                    parts = data.split(",")
                    hive = int(parts[0].split("=")[1])
                    no = int(parts[1].split("=")[1])
                    
                    # Run Pi5 capture script (Front)
                    script_path = os.path.join(os.path.dirname(__file__), "Capture_Bee_Back.py")
                    subprocess.run(["python3", script_path, str(hive), str(no)])

                    # Step 3: pause before connecting back
                    time.sleep(3)

                    # Step 3: connect back to Pi5 on port 5001 and send
                    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s_img:
                        s_img.connect((PI5_IP, PI5_IMG_PORT))
                        header = f"HIVE={hive},NO={no},FILENAME={filename}\n"
                        s_img.sendall(header.encode("utf-8"))
                        with open(filename, "rb") as f:
                            while True:
                                chunk = f.read(4096)
                                if not chunk:
                                    break
                                s_img.sendall(chunk)
                        print(f"Image {filename} sent to Pi5")
                except Exception as e:
                    print(f"Error parsing or sending capture: {e}")
