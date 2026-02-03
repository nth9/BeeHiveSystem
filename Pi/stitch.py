import cv2
import numpy as np
import os
import sys
from collections import namedtuple

# --- Configuration and File Paths ---
# NOTE: Replace these paths with your actual image files.
# The images must have good, distinct overlap.
IMAGE_PATHS = [
    'image/551503990_1312690320462694_8480374002238929892_n.jpg',
    'image/551433788_2568930676809367_3792910353986624383_n.jpg',
    'image/551284091_1867896990607679_2527545626479210072_n.jpg'
]
OUTPUT_PATH = 'stitched_perspective_panorama.jpg' # Updated output name
RANSAC_REPROJ_THRESHOLD = 5.0 # Max distance for points to be considered inliers

# Define a named tuple to hold detected features
Features = namedtuple("Features", ["keypoints", "descriptors"])

def load_images(paths):
    """Loads images and checks for file existence."""
    images = []
    print("--- Loading Input Images ---")
    for path in paths:
        if not os.path.exists(path):
            print(f"ERROR: Image file not found: {path}")
            sys.exit(1)
        img = cv2.imread(path)
        if img is None:
            print(f"ERROR: Could not read image: {path}.")
            sys.exit(1)
        # Ensure images are not too large for processing (e.g., resize if necessary, but skipping for now)
        print(f"Loaded {path} with dimensions: {img.shape}")
        images.append(img)
    return images

def detect_and_match_features(img1, img2):
    """Detects ORB features and matches them using a Brute Force matcher."""
    # Initialize ORB detector
    orb = cv2.ORB_create(nfeatures=10000)

    # Find keypoints and descriptors
    kp1, des1 = orb.detectAndCompute(img1, None)
    kp2, des2 = orb.detectAndCompute(img2, None)
    
    if des1 is None or des2 is None or len(des1) < 2 or len(des2) < 2:
        print("Warning: Not enough features detected in one or both images.")
        return None

    # Brute Force Matcher (using Hamming distance for ORB descriptors)
    bf = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=False)

    # Perform k-NN matching (k=2 for Lowe's ratio test)
    raw_matches = bf.knnMatch(des1, des2, k=2)

    # Apply Lowe's ratio test to filter good matches
    good_matches = []
    for m, n in raw_matches:
        if m.distance < 0.75 * n.distance:
            good_matches.append(m)
            
    print(f"Found {len(good_matches)} good matches after ratio test.")
    
    if len(good_matches) < 4:
        print("ERROR: Too few reliable matches (RANSAC needs at least 4).")
        return None
        
    return Features(keypoints=(kp1, kp2), descriptors=good_matches)


def estimate_homography(features):
    """
    *** MODIFIED FOR PANORAMIC STITCHING ***
    Estimates a full 3x3 Homography (Perspective) matrix H using RANSAC.
    This corrects for camera rotation and perspective change, creating a true panorama.
    """
    kp1, kp2 = features.keypoints
    good_matches = features.descriptors
    
    # Extract the coordinates of the matched keypoints
    pts1 = np.float32([kp1[m.queryIdx].pt for m in good_matches]).reshape(-1, 1, 2)
    pts2 = np.float32([kp2[m.trainIdx].pt for m in good_matches]).reshape(-1, 1, 2)

    # Use findHomography (Perspective) transformation estimation
    H, mask = cv2.findHomography(pts2, pts1, method=cv2.RANSAC, 
                                 ransacReprojThreshold=RANSAC_REPROJ_THRESHOLD)
                                          
    if H is None:
        raise ValueError("Homography transformation estimation failed.")
    
    return H, mask

def draw_inlier_matches(img1, img2, kp1, kp2, matches, mask, output_filename):
    """
    Draws the inlier matches between two images and saves the visualization.
    Only matches accepted by RANSAC (inliers) are drawn.
    """
    # Create an array of match indices based on the mask
    # We convert the 1D mask to a list of lists of 1s and 0s as required by drawMatches
    match_mask = mask.ravel().tolist()
    
    # Draw the inlier matches in green, and rejected matches (outliers) in red/gray.
    # We use a white color scheme for simplicity and clarity on the connecting lines.
    drawn_matches = cv2.drawMatches(img1, kp1, img2, kp2, matches, None,
                                   matchColor=(0, 255, 0), # Green for inliers
                                   singlePointColor=(0, 0, 255), # Blue for keypoints
                                   matchesMask=match_mask,
                                   flags=cv2.DrawMatchesFlags_NOT_DRAW_SINGLE_POINTS)
                                   
    cv2.imwrite(output_filename, drawn_matches)
    print(f"Visualized inlier matches saved to: {output_filename}")


def stitch_pair(img_ref, img_target, match_output_name):
    """
    Stitches the target image onto the reference image using a calculated
    Homography (Perspective) transformation and returns the result.
    """
    print(f"\n--- Aligning Target onto Reference ---")
    features = detect_and_match_features(img_ref, img_target)
    
    if features is None:
        raise ValueError("Stitching aborted: Feature matching failed.")
        
    # Get the Homography matrix (H is 3x3)
    H, mask = estimate_homography(features)
    
    # Draw the successful inlier matches for visualization
    kp_ref, kp_target = features.keypoints
    draw_inlier_matches(img_ref, img_target, kp_ref, kp_target, 
                        features.descriptors, mask, match_output_name)


    # Extract dimensions
    h_ref, w_ref = img_ref.shape[:2]
    h_target, w_target = img_target.shape[:2]

    # 1. Transform the four corners of the target image to find the new canvas size
    target_corners = np.float32([[0, 0], [w_target, 0], [w_target, h_target], [0, h_target]]).reshape(-1, 1, 2)
    # Use cv2.perspectiveTransform for the 3x3 Homography matrix H
    transformed_corners = cv2.perspectiveTransform(target_corners, H)

    # 2. Combine reference corners with transformed target corners
    all_corners = np.vstack(([[0, 0], [w_ref, 0], [w_ref, h_ref], [0, h_ref]], transformed_corners.reshape(-1, 2))).astype(np.int32)

    # 3. Calculate the bounding box of the final panorama
    [x_min, y_min] = np.min(all_corners, axis=0)
    [x_max, y_max] = np.max(all_corners, axis=0)
    
    # Calculate dimensions of the new canvas
    new_w = x_max - x_min
    new_h = y_max - y_min
    
    # 4. Create the final transformation matrix T (Translation to shift everything back to origin)
    T = np.array([
        [1, 0, -x_min],
        [0, 1, -y_min],
        [0, 0, 1]
    ], dtype=np.float32)

    # 5. Adjust the Homography matrix H to the new canvas coordinate system
    # M_stitched = T * H (where T shifts the origin and H warps the perspective)
    M_stitched = T @ H
    
    # 6. Warp the target image onto the new canvas
    warped_target = cv2.warpPerspective(img_target, M_stitched, (new_w, new_h))
    
    # 7. Place the reference image (I1) on the new canvas (only translation T is needed)
    warped_ref = cv2.warpPerspective(img_ref, T, (new_w, new_h))

    # 8. Final Composition (Simple Overlay with Blending Fix)
    
    # Create a 2D mask where the target image (warped_target) is not black (0, 0, 0).
    target_is_valid_2d = np.sum(warped_target, axis=2) > 0

    # Create a 3D mask by stacking the 2D mask 3 times (for B, G, R channels)
    target_is_valid_3d = np.dstack([target_is_valid_2d] * 3)

    # Use the 3D mask to combine the images: 
    # Wherever the target is valid (not black background), use the target pixel. 
    # Otherwise, use the reference pixel. This effectively overlays the target image.
    result = np.where(target_is_valid_3d, warped_target, warped_ref)
    
    return result

def main():
    try:
        images = load_images(IMAGE_PATHS)
        if len(images) != 3:
            print("Expected 3 input images but found 0.")
            return

        img1, img2, img3 = images

        print("\n--- STAGE 1: Stitching Image 1 and Image 2 (Full Perspective) ---")
        # I1 is the reference, I2 is the target
        pano_a = stitch_pair(img1, img2, 'matches_stage1_1_vs_2_perspective.jpg')
        print("STAGE 1 Complete. Intermediate panorama created.")

        print("\n--- STAGE 2: Stitching Pano A and Image 3 (Full Perspective) ---")
        # Pano A is the new reference, I3 is the target
        final_panorama = stitch_pair(pano_a, img3, 'matches_stage2_pano_vs_3_perspective.jpg')
        print("STAGE 2 Complete. Final panorama created.")

        # 4. Save the final result
        cv2.imwrite(OUTPUT_PATH, final_panorama)
        print(f"\nSUCCESS: Images stitched using Homography. Output saved to: {OUTPUT_PATH}")
        print("This stitched result uses Homography (Perspective) transformation, which is the standard panoramic method. Expect potential 'fisheye' distortion on large scenes.")

    except Exception as e:
        print(f"\nAn error occurred during stitching: {e}")
        print("Please ensure your input images have clear, overlapping features.")

if __name__ == '__main__':
    # Set logging level for better visibility of potential issues (optional)
    # cv2.setLogLevel(cv2.LOG_LEVEL_INFO) 
    main()
