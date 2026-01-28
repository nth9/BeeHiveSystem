import subprocess
import time
import sys

def capture_cam0(hive, no):
    print("Waiting 6 seconds before capture...")
    time.sleep(6)

    filename = f"Bee_Front_{hive}_{no}_Right.jpg"

    subprocess.run([
        "rpicam-still",
        "--camera", "0",
        "-t", "5000",
        "-n",
        "--autofocus-on-capture", "on",
        "-o", filename
    ])
    print(f"Bee capture completed (cam0). Saved as {filename}")

    # --- Send to Pi5 via LAN ---
    dest = f"pi@10.10.10.5:/home/pi/bee_images/"
    subprocess.run(["scp", filename, dest])
    print(f"Image sent to Pi5 at {dest}")

def main():
    if len(sys.argv) != 3:
        print("Usage: Capture_bee.py <HIVE> <NO>")
        sys.exit(1)

    hive = int(sys.argv[1])
    no = int(sys.argv[2])
    capture_cam0(hive, no)

if __name__ == "__main__":
    main()
