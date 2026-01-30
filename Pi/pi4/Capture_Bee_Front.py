import subprocess
import time
import sys

def capture_cam0(hive, no):
    print("Waiting 6 seconds before capture...")
    time.sleep(6)

    filename = f"/home/bee-pi4/bee_image/Bee_Front_{hive}_{no}_Left%04d.jpg"

    subprocess.run([
        "rpicam-still",
        "--camera", "0",
        "-t", "5000",
        "-n",
        "--autofocus-on-capture", "on",
        "--timeout", "30000",
        "--timelapse", "2000",
        "-o", filename
    ])
    print(f"Bee capture completed (cam0). Saved as {filename}")

def main():
    if len(sys.argv) != 3:
        print("Usage: Capture_bee.py <HIVE> <NO>")
        sys.exit(1)

    hive = int(sys.argv[1])
    no = int(sys.argv[2])
    capture_cam0(hive, no)

if __name__ == "__main__":
    main()
