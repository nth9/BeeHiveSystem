import os
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload
from google_auth_oauthlib.flow import InstalledAppFlow
from google.auth.transport.requests import Request
import pickle

# Configure these!
FOLDER_PATH = 'home/beepi/bee_images/combine'  # Replace with your image folder path
DRIVE_FOLDER_ID = '1RAeiuc9nVkS1wE0EDTD5KLGfrelw7f1i'   # Optional: set to a specific Drive folder
SCOPES = ['https://www.googleapis.com/auth/drive.file']

def authenticate():
    creds = None
    if os.path.exists('token.pickle'):
        with open('token.pickle', 'rb') as token:
            creds = pickle.load(token)
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            flow = InstalledAppFlow.from_client_secrets_file('credentials.json', SCOPES)
            creds = flow.run_local_server(port=0)
        with open('token.pickle', 'wb') as token:
            pickle.dump(creds, token)
    return creds

def upload_images():
    creds = authenticate()
    service = build('drive', 'v3', credentials=creds)
    for filename in os.listdir(FOLDER_PATH):
        if filename.lower().endswith(('.png', '.jpg', '.jpeg', '.gif')):
            filepath = os.path.join(FOLDER_PATH, filename)
            file_metadata = {'name': filename}
            if DRIVE_FOLDER_ID:
                file_metadata['parents'] = [DRIVE_FOLDER_ID]
            media = MediaFileUpload(filepath, mimetype='image/jpeg')
            file = service.files().create(body=file_metadata, media_body=media, fields='id').execute()
            print(f'Uploaded {filename}, file ID: {file.get("id")}')

if __name__ == '__main__':

    upload_images()
