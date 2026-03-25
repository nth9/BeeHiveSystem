import cv2
import os
import re
import math
import numpy as np
from ultralytics import YOLO

# =============== CONFIG ===============
INPUT_IMAGE   = "runs/stitched_left_middle_right_23_set.jpg"
SLICES_FOLDER = "slices"
MODEL_PATH    = "cellv4.pt"
OUTPUT_IMAGE  = "detected_full_7.png"
CONF_THRESH   = 0.50
TILE_SIZE     = 640
OVERLAP       = 150
OVERLAP_THRESH = 0.40   # suppress circle if it overlaps >40% with a more-confident one
# =====================================

# Load original image to get full dimensions
orig = cv2.imread(INPUT_IMAGE)
if orig is None:
    raise FileNotFoundError(f"[ERROR] Cannot load: {INPUT_IMAGE}")
H, W = orig.shape[:2]
print(f"[INFO] Original size: {W}x{H}")

# Canvas to draw results on
canvas = orig.copy()

# Load model
model = YOLO(MODEL_PATH)
print(f"[INFO] Model loaded: {MODEL_PATH}")

# Auto-generate distinct BGR colors per class index
def class_color(cls_id):
    np.random.seed(cls_id + 42)
    return tuple(int(c) for c in np.random.randint(50, 255, 3))


def circle_intersection_area(cx1, cy1, r1, cx2, cy2, r2):
    """Exact area of intersection of two circles."""
    d = math.hypot(cx2 - cx1, cy2 - cy1)
    if d >= r1 + r2:          # no overlap
        return 0.0
    if d <= abs(r1 - r2):     # one circle fully inside the other
        return math.pi * min(r1, r2) ** 2
    a1 = math.acos((d * d + r1 * r1 - r2 * r2) / (2 * d * r1))
    a2 = math.acos((d * d + r2 * r2 - r1 * r1) / (2 * d * r2))
    triangle = 0.5 * math.sqrt((-d + r1 + r2) * (d + r1 - r2) * (d - r1 + r2) * (d + r1 + r2))
    return r1 * r1 * a1 + r2 * r2 * a2 - triangle


def circle_overlap_ratio(cx1, cy1, r1, cx2, cy2, r2):
    """Overlap as a fraction of the smaller circle's area."""
    inter = circle_intersection_area(cx1, cy1, r1, cx2, cy2, r2)
    smaller_area = math.pi * min(r1, r2) ** 2
    return inter / smaller_area if smaller_area > 0 else 0.0


# Collect all detections across tiles
detections = []   # list of (conf, cx, cy, r, cls, label)

# Process each tile
tile_files = sorted(f for f in os.listdir(SLICES_FOLDER) if f.endswith(".png"))

for fname in tile_files:
    # Parse global offset from filename: tile_0000_x128_y256.png
    m = re.search(r"_x(\d+)_y(\d+)", fname)
    if m is None:
        print(f"[WARN] Skipping {fname} — cannot parse offset")
        continue
    ox, oy = int(m.group(1)), int(m.group(2))

    tile_path = os.path.join(SLICES_FOLDER, fname)
    tile      = cv2.imread(tile_path)
    if tile is None:
        continue

    results = model(tile, conf=CONF_THRESH, verbose=False)[0]

    for box in results.boxes:
        x1, y1, x2, y2 = box.xyxy[0].tolist()
        conf  = float(box.conf[0])
        cls   = int(box.cls[0])
        label = model.names[cls]

        # Convert tile-local coords to global image coords
        gx1 = int(x1) + ox
        gy1 = int(y1) + oy
        gx2 = int(x2) + ox
        gy2 = int(y2) + oy

        # Circle: center + radius from bounding box
        cx = (gx1 + gx2) // 2
        cy = (gy1 + gy2) // 2
        r  = max((gx2 - gx1), (gy2 - gy1)) // 2

        # Clamp to image bounds
        cx = max(r, min(W - r, cx))
        cy = max(r, min(H - r, cy))

        detections.append((conf, cx, cy, r, cls, label))

# Greedy NMS: sort by confidence descending, keep circle only if it doesn't
# overlap >40% with any already-kept circle.
detections.sort(key=lambda d: d[0], reverse=True)
kept = []
for det in detections:
    conf, cx, cy, r, cls, label = det
    suppress = False
    for kconf, kcx, kcy, kr, kcls, klabel in kept:
        if circle_overlap_ratio(cx, cy, r, kcx, kcy, kr) > OVERLAP_THRESH:
            suppress = True
            break
    if not suppress:
        kept.append(det)

# Draw kept detections
for conf, cx, cy, r, cls, label in kept:
    color = class_color(cls)
    cv2.circle(canvas, (cx, cy), r, color, 5)
    cv2.putText(
        canvas,
        f"{label} {conf:.2f}",
        (cx - r, cy - r - 4),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.45, color, 1, cv2.LINE_AA,
    )

cv2.imwrite(OUTPUT_IMAGE, canvas)
print(f"[DONE] {len(kept)}/{len(detections)} detections kept after overlap NMS  ->  {OUTPUT_IMAGE}")
