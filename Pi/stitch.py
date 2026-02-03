import cv2
import numpy as np
import os
import sys
import re
from collections import namedtuple, defaultdict

# ==========================================
# --- CONFIGURATION ---
# ==========================================

# 1. INPUT FOLDERS (Processed in this order)
INPUT_FOLDERS = [
    "home/beepi/bee_images/front",
    "home/beepi/bee_images/back"
]

# 2. OUTPUT FOLDERS
OUTPUT_FOLDER = "home/beepi/bee_images/combine"

# 3. FILE PATTERN MATCHING
# Regex to handle both "Cell_" and "Bee_" prefixes
# Examples: 
#   "Cell_Front_H01_S01_Left.jpg" (Has Middle)
#   "Bee_Back_H02_S05_Right.jpg"  (No Middle)
FILENAME_REGEX = r"(Cell|Bee)_([A-Za-z]+)_H(\d+)_S(\d+)_([A-Za-z]+).*\.jpg"

# 4. TUNING
RANSAC_REPROJ_THRESHOLD = 5.0
Features = namedtuple("Features", ["keypoints", "descriptors"])

# ==========================================
# --- IMAGE PROCESSING FUNCTIONS ---
# ==========================================

def detect_and_match_features(img1, img2):
    """Detects ORB features and matches them using Lowe's Ratio Test."""
    orb = cv2.ORB_create(nfeatures=5000)
    kp1, des1 = orb.detectAndCompute(img1, None)
    kp2, des2 = orb.detectAndCompute(img2, None)
    
    if des1 is None or des2 is None: return None

    bf = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=False)
    raw_matches = bf.knnMatch(des1, des2, k=2)

    good_matches = []
    for m, n in raw_matches:
        if m.distance < 0.75 * n.distance:
            good_matches.append(m)
            
    if len(good_matches) < 4: return None
        
    return Features(keypoints=(kp1, kp2), descriptors=good_matches)

def estimate_homography(features):
    kp1, kp2 = features.keypoints
    good_matches = features.descriptors
    pts1 = np.float32([kp1[m.queryIdx].pt for m in good_matches]).reshape(-1, 1, 2)
    pts2 = np.float32([kp2[m.trainIdx].pt for m in good_matches]).reshape(-1, 1, 2)
    H, mask = cv2.findHomography(pts2, pts1, cv2.RANSAC, RANSAC_REPROJ_THRESHOLD)
    return H, mask

def warp_images(img_ref, img_target, H):
    """Warps target image to reference image plane."""
    h_ref, w_ref = img_ref.shape[:2]
    h_target, w_target = img_target.shape[:2]

    # Calculate new canvas size
    corners_target = np.float32([[0, 0], [w_target, 0], [w_target, h_target], [0, h_target]]).reshape(-1, 1, 2)
    corners_trans = cv2.perspectiveTransform(corners_target, H)
    corners_ref = np.float32([[0, 0], [w_ref, 0], [w_ref, h_ref], [0, h_ref]]).reshape(-1, 1, 2)
    
    all_corners = np.vstack((corners_ref, corners_trans))
    [x_min, y_min] = np.int32(all_corners.min(axis=0).ravel() - 0.5)
    [x_max, y_max] = np.int32(all_corners.max(axis=0).ravel() + 0.5)
    
    # Translation matrix
    t_vals = [-x_min, -y_min]
    T = np.array([[1, 0, t_vals[0]], [0, 1, t_vals[1]], [0, 0, 1]])
    final_H = T.dot(H)
    new_w, new_h = x_max - x_min, y_max - y_min
    
    # Warp images
    warped_target = cv2.warpPerspective(img_target, final_H, (new_w, new_h))
    warped_ref = cv2.warpPerspective(img_ref, T, (new_w, new_h))
    
    # Create mask to overlay non-black pixels
    dummy_target = np.ones((h_target, w_target), dtype=np.uint8) * 255
    mask_target = cv2.warpPerspective(dummy_target, final_H, (new_w, new_h))
    
    # Combine
    result = warped_ref.copy()
    np.copyto(result, warped_target, where=mask_target[:,:,None].astype(bool))
    
    return result

def stitch_3_images(img_left, img_middle, img_right):
    """
    Strategy for 'Cell' images:
    1. Stitch Left -> Middle (Middle is anchor)
    2. Stitch Right -> Result
    """
    # 
    # 1. Left onto Middle
    features_lm = detect_and_match_features(img_middle, img_left)
    if features_lm is None:
        print("    [Warning] Match failed: Left -> Middle. Using Middle only.")
        pano_lm = img_middle
    else:
        H_lm, _ = estimate_homography(features_lm)
        pano_lm = warp_images(img_middle, img_left, H_lm)

    # 2. Right onto Previous Result
    features_mr = detect_and_match_features(pano_lm, img_right)
    if features_mr is None:
        print("    [Warning] Match failed: Right -> Panorama. Returning partial.")
        return pano_lm
        
    H_mr, _ = estimate_homography(features_mr)
    final_pano = warp_images(pano_lm, img_right, H_mr)
    return final_pano

def stitch_2_images(img_left, img_right):
    """
    Strategy for 'Bee' images:
    1. Stitch Right -> Left (Left is anchor)
    """
    # 
    features_lr = detect_and_match_features(img_left, img_right)
    if features_lr is None:
        print("    [Error] Match failed: Left <-> Right.")
        return None
        
    H_lr, _ = estimate_homography(features_lr)
    # Warp Right onto Left
    final_pano = warp_images(img_left, img_right, H_lr)
    return final_pano

# ==========================================
# --- FILE MANAGEMENT & MAIN ---
# ==========================================

def get_image_groups(folder):
    """
    Scans folder and groups images.
    Returns: dict[key] = { 'left': path, 'right': path, ... }
    Key = (PrefixType, Position, Hive, SubjectNo)
    """
    groups = defaultdict(dict)
    
    if not os.path.exists(folder):
        print(f"Warning: Folder '{folder}' not found.")
        return {}

    files = [f for f in os.listdir(folder) if f.lower().endswith(('.jpg', '.jpeg', '.png'))]
    print(f"  > Scanning {len(files)} files in {folder}...")

    for filename in files:
        # Regex Match
        # Group 1: Cell/Bee, 2: Pos, 3: Hive, 4: No, 5: View
        match = re.search(FILENAME_REGEX, filename)
        if match:
            prefix = match.group(1) # Cell or Bee
            pos = match.group(2)    # Front or Back
            hive = match.group(3)   # 01
            no = match.group(4)     # 01
            view = match.group(5)   # Left/Right/Middle

            # Unique Key
            group_key = (prefix, pos, hive, no)
            
            view_lower = view.lower()
            if "left" in view_lower: groups[group_key]['left'] = filename
            elif "right" in view_lower: groups[group_key]['right'] = filename
            elif "middle" in view_lower: groups[group_key]['middle'] = filename
        else:
            # Optional: print skipped files
            pass

    return groups

def process_folder(folder_path):
    """Process a single folder (Front or Back)"""
    print(f"\n[{folder_path}] Starting processing...")
    groups = get_image_groups(folder_path)
    
    if not groups:
        print("  > No matching image groups found.")
        return

    sorted_keys = sorted(groups.keys()) # Process in order
    
    for (prefix, pos, hive, no) in sorted_keys:
        images = groups[(prefix, pos, hive, no)]
        print(f"\n--- Processing: {prefix} | {pos} | H{hive} | S{no} ---")

        # 1. LOAD IMAGES
        img_l = cv2.imread(os.path.join(folder_path, images.get('left', ''))) if 'left' in images else None
        img_r = cv2.imread(os.path.join(folder_path, images.get('right', ''))) if 'right' in images else None
        img_m = cv2.imread(os.path.join(folder_path, images.get('middle', ''))) if 'middle' in images else None

        result = None

        # 2. DECIDE STRATEGY
        if prefix == "Cell":
            # Expect Left, Middle, Right
            if img_l is not None and img_m is not None and img_r is not None:
                print("  > Mode: 3-Image Stitch (Cell)")
                result = stitch_3_images(img_l, img_m, img_r)
            else:
                print(f"  > [Skipping] 'Cell' requires 3 images. Found: {list(images.keys())}")
                continue
        
        elif prefix == "Bee":
            # Expect Left, Right (No Middle)
            if img_l is not None and img_r is not None:
                print("  > Mode: 2-Image Stitch (Bee)")
                result = stitch_2_images(img_l, img_r)
            else:
                print(f"  > [Skipping] 'Bee' requires Left & Right. Found: {list(images.keys())}")
                continue

        # 3. SAVE RESULT
        if result is not None:
            # Output Name: Cell_Front_H01_S01.jpg (We keep 'Cell' prefix for consistency or use variable)
            # User requested: "Cell_(front first then back)_H{HIVE}_S{NO}..."
            # We will use the prefix found in the file (Cell or Bee)
            out_name = f"{prefix}_{pos}_H{hive}_S{no}.jpg"
            out_path = os.path.join(OUTPUT_FOLDER, out_name)
            
            cv2.imwrite(out_path, result)
            print(f"  > [Success] Saved: {out_name}")

def main():
    # Create Output Dir
    if not os.path.exists(OUTPUT_FOLDER):
        os.makedirs(OUTPUT_FOLDER)

    # Process FRONT folders first, then BACK
    for folder in INPUT_FOLDERS:
        process_folder(folder)

    print("\nAll processing complete.")

if __name__ == '__main__':
    main()
