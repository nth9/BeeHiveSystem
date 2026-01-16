# cam_auto_calibrate_pi.py
import cv2
import numpy as np
import subprocess

def capture_images():
    cams = [0, 1]  # camera indices
    for cam in cams:
        subprocess.run([
            "rpicam-still",
            "--camera", str(cam),
            "-t", "2000",
            "-n",
            "--autofocus-on-capture", "on",
            "-o", f"cam{cam}.jpg"
        ])

def detect_squares(gray, min_size=50):
    edges = cv2.Canny(gray, 50, 150)
    contours, _ = cv2.findContours(edges, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    squares = []
    for cnt in contours:
        approx = cv2.approxPolyDP(cnt, 0.05*cv2.arcLength(cnt, True), True)
        if len(approx) == 4 and cv2.isContourConvex(approx):
            x, y, w, h = cv2.boundingRect(approx)
            if w > min_size and h > min_size:
                roi = gray[y:y+h, x:x+w]
                mean_val = np.mean(roi)
                squares.append((x, y, w, h, mean_val))
    return squares

def auto_calibrate(img_path, tol=10, max_iter=20):
    img = cv2.imread(img_path, cv2.IMREAD_COLOR)
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

    alpha, beta = 1.0, 0.0  # initial calibration values

    for i in range(max_iter):
        squares = detect_squares(gray)
        if not squares:
            print(f"No squares found in {img_path}")
            break

        # pick brightest and darkest square
        mean_vals = [s[4] for s in squares]
        white_sq = max(mean_vals)
        black_sq = min(mean_vals)

        print(f"Iter {i}: white={white_sq:.1f}, black={black_sq:.1f}, alpha={alpha}, beta={beta}")

        # check if normalized
        if abs(white_sq - 255) < tol and abs(black_sq - 0) < tol:
            print("Calibration successful")
            break

        # adjust brightness/contrast
        if white_sq < 255:
            alpha += 0.05
            beta += 5
        if black_sq > 0:
            alpha -= 0.05
            beta -= 5

        gray = cv2.convertScaleAbs(gray, alpha=alpha, beta=beta)

    # save calibrated image
    calibrated_path = img_path.replace(".jpg", "_cal.jpg")
    cv2.imwrite(calibrated_path, gray)

    # save calibration values
    txt_path = img_path.replace(".jpg", "_cal.txt")
    with open(txt_path, "w") as f:
        f.write(f"alpha={alpha}\n")
        f.write(f"beta={beta}\n")

    print(f"Saved calibrated image: {calibrated_path}")
    print(f"Saved calibration values: {txt_path}")

def main():
    print("Capturing images...")
    capture_images()
    print("Auto-calibrating images...")
    for cam in [0, 1]:
        auto_calibrate(f"cam{cam}.jpg")

if __name__ == "__main__":
    main()
