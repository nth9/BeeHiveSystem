import serial
import subprocess
import os
import socket

# --- UART setup ---
ser = serial.Serial("/dev/serial0", baudrate=9600, timeout=1)

# --- LAN setup ---
PI4_IP = "192.168.1.10"
PI4_CMD_PORT = 5000   # Pi4 command listener
PI5_IMG_PORT = 5001   # Pi5 image receiver

def notify_pi4(hive, no):
    """Send command to Pi4 on port 5000."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((PI4_IP, PI4_CMD_PORT))
        message = f"HIVE={hive},NO={no}"
        s.sendall(message.encode("utf-8"))
        print(f"Sent to Pi4: {message}")

def wait_for_image(hive, no):
    """Wait for Pi4 to connect back and send image on port 5001."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s_img:
        s_img.bind(("", PI5_IMG_PORT))
        s_img.listen()
        print(f"Pi5 waiting for image on port {PI5_IMG_PORT}...")
        conn, addr = s_img.accept()
        with conn:
            print(f"Image connection from {addr}")
            header = b""
            while not header.endswith(b"\n"):
                chunk = conn.recv(1)
                if not chunk:
                    break
                header += chunk
            header_str = header.decode("utf-8").strip()
            print(f"Header received: {header_str}")

            save_path = f"Bee_Front_H{hive}_S{no}_FromPi4.jpg"
            with open(save_path, "wb") as f:
                while True:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    f.write(chunk)
            print(f"Image saved from Pi4 as {save_path}")

def handle_uart_line(line):
    
    line = line.strip()
    if line.startswith("Capture Bee"):
        parts = line.split(",")
        hive, no = 0, 0
        for p in parts:
            if p.startswith("HIVE="):
                hive = int(p.split("=")[1])
            elif p.startswith("NO="):
                no = int(p.split("=")[1])

        print(f"Triggering Capture_Bee_Front.py with hive={hive}, no={no}")

        # Send command to Pi4
        notify_pi4(hive, no)

        # Run Pi5 capture script (Front)
        script_path = os.path.join(os.path.dirname(__file__), "Capture_Bee_Front.py")
        subprocess.run(["python3", script_path, str(hive), str(no)])
        
        # Wait for image back
        wait_for_image(hive, no)
        
        # Send command to esp32
        ser.write(b"SWEEP\n") 
        print("Sent 'SWEEP' to ESP32 via UART")
        
    if line.startswith("FINISH"):
    	parts = line.split(",")
        hive, no = 0, 0
        for p in parts:
            if p.startswith("HIVE="):
                hive = int(p.split("=")[1])
            elif p.startswith("NO="):
                no = int(p.split("=")[1])

        print(f"Triggering Capture_Bee_Front.py with hive={hive}, no={no}")

        # Send command to Pi4
        notify_pi4(hive, no)

        # Run Pi5 capture script (Back)
        script_path = os.path.join(os.path.dirname(__file__), "Capture_Bee_Back.py")
        subprocess.run(["python3", script_path, str(hive), str(no)])
        
        # wait for image back
        wait_for_image(hive, no)
        

def main():
    print("Listening on UART /dev/serial0 for 'Capture Bee' command...")
    while True:
        try:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                print(f"UART received: {line}")
                handle_uart_line(line)
        except Exception as e:
            print(f"Error reading UART: {e}")

if __name__ == "__main__":
    main()
