# cam_capture_pi5.py

import subprocess

def capture_images():
    # Camera 0
    subprocess.run([
        "rpicam-still",
        "--camera", "0",
        "-t", "5000",
        "-n",
        "--autofocus-on-capture", "on",
        "-o", "cam0.jpg"
    ])

    # Camera 1
    subprocess.run([
        "rpicam-still",
        "--camera", "1",
        "-t", "5000",
        "-n",
        "--autofocus-on-capture", "on",
        "-o", "cam1.jpg"
    ])

def main():
    print("Starting camera captures on Pi 5...")
    capture_images()
    print("Image capture completed.")

if __name__ == "__main__":
    main()
