import os
import glob
import cv2
import supervision as sv
# Updated import
from inference_sdk import InferenceHTTPClient, InferenceConfiguration 

# 1. SET API KEY (Masked for security)
os.environ["ROBOFLOW_API_KEY"] = "NCf9oiBta2Qzcb6EdiDb"

# 2. Setup Paths
input_pattern = r"runs/Bee_Front_H1_F3_Right_3.jpg" 
output_dir = r"runs/output"

if not os.path.exists(output_dir):
    os.makedirs(output_dir)

# 3. Initialize Client
CLIENT = InferenceHTTPClient(
    api_url="https://detect.roboflow.com",
    api_key=os.environ["ROBOFLOW_API_KEY"]
)

# --- OVERRIDE MAX DETECTIONS ---
CLIENT.configure(InferenceConfiguration(max_detections=1500))

# --- ADJUST ANNOTATOR SCALING ---
box_annotator = sv.BoxAnnotator(thickness=1)
# ... The rest of your processing loop remains exactly the same ...
label_annotator = sv.LabelAnnotator(
    text_scale=0.3, 
    text_thickness=1,
    text_padding=2
)

# 4. Process Images
for image_path in glob.glob(input_pattern):
    image = cv2.imread(image_path)
    if image is None: continue

    # Resize to prevent "413 Request Too Large"
    max_dim = 1280
    h, w = image.shape[:2]
    if w > max_dim or h > max_dim:
        scale = max_dim / max(w, h)
        image = cv2.resize(image, (int(w * scale), int(h * scale)))
        h, w = image.shape[:2] # Update h, w after resize

    # If filename contains "left"/"Left"/"LEFT", crop right 15% before detection
    file_name = os.path.basename(image_path)
    if any(keyword in file_name for keyword in ["left", "Left", "LEFT"]):
        crop_w = int(w * 0.75)
        image = image[:, :crop_w]
        h, w = image.shape[:2]

    try:
        result = CLIENT.infer(image, model_id="beeproject-uhq2j/15")
        detections = sv.Detections.from_inference(result)

        # Confidence labels for each bee
        labels = [
            f"{class_name} {confidence:.2f}"
            for class_name, confidence 
            in zip(detections['class_name'], detections.confidence)
        ]

        # Draw boxes and labels
        annotated_image = box_annotator.annotate(scene=image.copy(), detections=detections)
        annotated_image = label_annotator.annotate(scene=annotated_image, detections=detections, labels=labels)

        # --- DRAW TOTAL COUNT IN UPPER RIGHT CORNER ---
        count_text = f"Total Bees: {len(detections)}"
        font = cv2.FONT_HERSHEY_SIMPLEX
        font_scale = 0.8
        thickness = 2
        
        # Get text size to position it correctly from the right edge
        text_size = cv2.getTextSize(count_text, font, font_scale, thickness)[0]
        text_x = w - text_size[0] - 20  # 20px padding from right
        text_y = 40                   # 40px padding from top

        # Draw a small background rectangle for the counter to make it readable
        cv2.rectangle(annotated_image, (text_x - 5, text_y - 30), (w, text_y + 10), (0, 0, 0), -1)
        # Draw the white text
        cv2.putText(annotated_image, count_text, (text_x, text_y), font, font_scale, (255, 255, 255), thickness)
        
        # Save
        save_path = os.path.join(output_dir, f"detected_{file_name}")
        cv2.imwrite(save_path, annotated_image)
        
        print(f"Processed {file_name}: {len(detections)} bees found.")

    except Exception as e:
        print(f"Error: {e}")