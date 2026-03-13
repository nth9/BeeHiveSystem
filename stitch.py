import cv2
import numpy as np
import os
import gc
from collections import namedtuple

# --- CUDA Setup ---
USE_CUDA = cv2.cuda.getCudaEnabledDeviceCount() > 0
if USE_CUDA:
    cv2.cuda.setDevice(0)
    print("CUDA GPU detected — using GPU acceleration")
else:
    print("No CUDA GPU found — running on CPU")

# --- Configuration ---
LEFT_IMAGE_PATHS = [
    'images/Cell_Front_H2_F4_Left.jpg'
]

MIDDLE_IMAGE_PATHS = [
    'images/Cell_Front_H2_F4_Middle.jpg'
]

RIGHT_IMAGE_PATHS = [
    'images/Cell_Front_H2_F4_Right.jpg'
]

OUTPUT_PATH = 'images/stitched_left_middle_right_1.jpg'
CALC_SCALE_PERCENT = 10.0
RATIO_TEST = 0.8

Features = namedtuple("Features", ["keypoints", "descriptors"])

def resize_image(image, scale_percent):
    width  = int(image.shape[1] * scale_percent / 100)
    height = int(image.shape[0] * scale_percent / 100)
    if USE_CUDA:
        gpu = cv2.cuda_GpuMat()
        gpu.upload(image)
        # CUDA resize does not support INTER_AREA; INTER_LINEAR is fine for small downscales
        gpu_out = cv2.cuda.resize(gpu, (width, height), interpolation=cv2.INTER_LINEAR)
        return gpu_out.download()
    return cv2.resize(image, (width, height), interpolation=cv2.INTER_AREA)

def preload_and_downscale(paths, side_name):
    small_images = []
    print(f"\n--- Loading {side_name} Candidates ---")
    for path in paths:
        if not os.path.exists(path): continue
        img = cv2.imread(path)
        if img is None: continue
        small = resize_image(img, CALC_SCALE_PERCENT)
        small_images.append({'path': path, 'img': small})
        del img
        gc.collect()
    return small_images

def _sift_cpu(img):
    sift = cv2.SIFT_create(nfeatures=20000, contrastThreshold=0.02, edgeThreshold=10)
    return sift.detectAndCompute(img, None)

def _sift_gpu(img):
    """Returns (keypoints_list, descriptors_ndarray) using CUDA SIFT."""
    sift_cuda = cv2.cuda.SIFT_create(nfeatures=20000, contrastThreshold=0.02, edgeThreshold=10)
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    gpu_gray = cv2.cuda_GpuMat()
    gpu_gray.upload(gray)
    kp_gpu, des_gpu = sift_cuda.detectAndComputeAsync(gpu_gray, None)
    kp  = sift_cuda.convert(kp_gpu)
    des = des_gpu.download()
    return kp, des

def detect_and_match_features(img1, img2):
    if USE_CUDA:
        try:
            kp1, des1 = _sift_gpu(img1)
            kp2, des2 = _sift_gpu(img2)
        except cv2.error:
            # CUDA SIFT not available in this build — fall back to CPU
            kp1, des1 = _sift_cpu(img1)
            kp2, des2 = _sift_cpu(img2)
    else:
        kp1, des1 = _sift_cpu(img1)
        kp2, des2 = _sift_cpu(img2)

    if des1 is None or des2 is None or len(des1) < 2 or len(des2) < 2:
        return None

    bf = cv2.BFMatcher()
    raw_matches = bf.knnMatch(des1, des2, k=2)
    good_matches = [m for m, n in raw_matches if m.distance < RATIO_TEST * n.distance]

    if len(good_matches) < 4:
        return None

    return Features(keypoints=(kp1, kp2), descriptors=good_matches)

def find_best_pair(left_data, right_data):
    print("\n--- Evaluating Best Left-Right Pair ---")
    best_pair  = None
    max_matches = -1

    for l in left_data:
        for r in right_data:
            features = detect_and_match_features(l['img'], r['img'])
            if features:
                num_matches = len(features.descriptors)
                print(f"Match: {os.path.basename(l['path'])} + {os.path.basename(r['path'])} -> {num_matches} points")
                if num_matches > max_matches:
                    max_matches = num_matches
                    best_pair = (l['path'], r['path'])

    return best_pair, max_matches

def estimate_transform(features):
    kp1, kp2 = features.keypoints
    pts1 = np.float32([kp1[m.queryIdx].pt for m in features.descriptors]).reshape(-1, 1, 2)
    pts2 = np.float32([kp2[m.trainIdx].pt for m in features.descriptors]).reshape(-1, 1, 2)

    matrix, mask = cv2.findHomography(pts2, pts1, cv2.RANSAC, 5.0)
    if matrix is None: raise ValueError("Transformation failed.")
    return matrix, mask

def compute_H_full(small_ref, small_target):
    """Returns homography mapping full_target -> full_ref coordinate space."""
    features = detect_and_match_features(small_ref, small_target)
    if features is None:
        raise ValueError("Feature matching failed.")
    H_small, _ = estimate_transform(features)
    scale_factor = CALC_SCALE_PERCENT / 100.0
    S     = np.diag([scale_factor, scale_factor, 1.0])
    S_inv = np.diag([1.0/scale_factor, 1.0/scale_factor, 1.0])
    return S_inv @ H_small @ S

def transform_corners(w, h, H):
    corners = np.float32([[0,0],[w,0],[w,h],[0,h]]).reshape(-1,1,2)
    return cv2.perspectiveTransform(corners, H).reshape(-1,2)

def _warp(img, H, new_w, new_h):
    if USE_CUDA:
        gpu_img = cv2.cuda_GpuMat()
        gpu_img.upload(img)
        gpu_warped = cv2.cuda.warpPerspective(gpu_img, H, (new_w, new_h))
        return gpu_warped.download()
    return cv2.warpPerspective(img, H, (new_w, new_h))

def stitch_three(full_left, full_middle, full_right):
    small_left   = resize_image(full_left,   CALC_SCALE_PERCENT)
    small_middle = resize_image(full_middle, CALC_SCALE_PERCENT)
    small_right  = resize_image(full_right,  CALC_SCALE_PERCENT)

    # Compute homographies: left->middle and right->middle (middle = identity)
    H_left  = compute_H_full(small_middle, small_left)
    H_right = compute_H_full(small_middle, small_right)
    H_mid   = np.eye(3, dtype=np.float64)

    h_l, w_l = full_left.shape[:2]
    h_m, w_m = full_middle.shape[:2]
    h_r, w_r = full_right.shape[:2]

    # Canvas bounds covering all three images
    all_corners = np.vstack([
        transform_corners(w_l, h_l, H_left),
        transform_corners(w_m, h_m, H_mid),
        transform_corners(w_r, h_r, H_right),
    ])
    x_min, y_min = np.floor(np.min(all_corners, axis=0)).astype(int)
    x_max, y_max = np.ceil(np.max(all_corners, axis=0)).astype(int)

    T = np.array([[1,0,-x_min],[0,1,-y_min],[0,0,1]], dtype=np.float64)
    new_w, new_h = x_max - x_min, y_max - y_min

    # Accumulate blend one image at a time to avoid holding all warped images in RAM
    accum   = np.zeros((new_h, new_w, 3), dtype=np.float32)
    total_w = np.zeros((new_h, new_w),    dtype=np.float32)

    for img, H in [(full_left, T @ H_left), (full_middle, T @ H_mid), (full_right, T @ H_right)]:
        warped = _warp(img, H, new_w, new_h)
        # distanceTransform has no CUDA equivalent — keep on CPU
        mask   = (np.sum(warped, axis=2) > 0).astype(np.uint8)
        weight = cv2.distanceTransform(mask, cv2.DIST_L2, 5).astype(np.float32)
        accum   += warped.astype(np.float32) * weight[:,:,None]
        total_w += weight
        del warped, mask, weight
        gc.collect()

    total_w[total_w == 0] = 1.0
    result = (accum / total_w[:,:,None]).astype(np.uint8)
    del accum, total_w
    gc.collect()
    return result

def main():
    left_cands  = preload_and_downscale(LEFT_IMAGE_PATHS,   "LEFT")
    mid_cands   = preload_and_downscale(MIDDLE_IMAGE_PATHS, "MIDDLE")
    right_cands = preload_and_downscale(RIGHT_IMAGE_PATHS,  "RIGHT")

    if not left_cands or not mid_cands or not right_cands:
        print("Missing candidates."); return

    print("\n--- Finding Best Left-Middle Pair ---")
    best_lm, _ = find_best_pair(left_cands, mid_cands)
    if not best_lm:
        print("No valid left-middle match found."); return

    print("\n--- Finding Best Middle-Right Pair ---")
    best_mr, _ = find_best_pair(mid_cands, right_cands)
    if not best_mr:
        print("No valid middle-right match found."); return

    print(f"\nStitching: {best_lm[0]} + {best_lm[1]} + {best_mr[1]}")

    full_left   = cv2.imread(best_lm[0])
    full_middle = cv2.imread(best_lm[1])
    full_right  = cv2.imread(best_mr[1])

    final_img = stitch_three(full_left, full_middle, full_right)

    cv2.imwrite(OUTPUT_PATH, final_img)
    print(f"Saved to {OUTPUT_PATH}")

if __name__ == '__main__':
    main()
