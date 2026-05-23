'''
Project: ESP32 Edge Capture Web App
Author: Eriberto Salgado

Description:
This Python Flask program connects to an ESP32 camera through Serial.
It captures an image, saves it locally, and uploads it to Edge Impulse
for training or testing.
'''

from flask import Flask, render_template, request, jsonify, send_from_directory
import os
import requests
import serial
import serial.tools.list_ports
import time
from io import BytesIO
from PIL import Image
import json

app = Flask(__name__)

# Get all available serial COM ports
def get_com_ports():
    return [port.device for port in serial.tools.list_ports.comports()]

# Folder where captured images will be saved
image_dir = "captured_images"

# Create image folder if it does not already exist
if not os.path.exists(image_dir):
    os.makedirs(image_dir)

# Home page route
# Displays the web page and available COM ports
@app.route('/')
def home():
    return render_template('index.html', com_ports=get_com_ports())

# Route used to capture an image and upload it
@app.route('/capture', methods=['POST'])
def capture():
    try:
        # Read form data from the webpage
        com_port = request.form['com_port']
        api_key = request.form['api_key']
        label = request.form['label']
        mode = request.form['mode']

        print(f"Mode: {mode}")
        print(f"Received label: {label}")
        print(f"Using COM port: {com_port}")

        # Choose Edge Impulse upload URL
        if mode == 'training':
            upload_url = 'https://ingestion.edgeimpulse.com/api/training/files'
        else:
            upload_url = 'https://ingestion.edgeimpulse.com/api/testing/files'

        print(f"Upload URL: {upload_url}")

        # Open Serial connection to ESP32
        ser = serial.Serial(com_port, 921600, timeout=10)
        time.sleep(2)

        # Clear old Serial data
        while ser.in_waiting:
            ser.readline()

        # Send capture command to ESP32
        ser.write(b'CAPTURE\n')

        # Read image size from ESP32
        size_line = ser.readline().decode().strip()
        print(f"Received size data: '{size_line}'")

        # Make sure the received size is valid
        if not size_line.isdigit():
            raise ValueError(f"Invalid image size: {size_line}")

        size = int(size_line)

        # Read image bytes from ESP32
        image_data = ser.read(size)

        # Close Serial connection
        ser.close()

        # Convert received bytes into an image
        img = Image.open(BytesIO(image_data))
        img_io = BytesIO()
        img.save(img_io, 'JPEG')
        img_io.seek(0)

        # Save captured image locally
        filename = "captured_image.jpg"
        filepath = os.path.join(image_dir, filename)

        with open(filepath, 'wb') as f:
            f.write(img_io.getvalue())

        # Image path used by the webpage
        image_url = f"/static/{filename}"

        # Edge Impulse upload headers
        headers = {
            'x-api-key': api_key,
            'x-add-date-id': '1',
        }

        # Image file to upload
        files = [
            ('data', (filename, open(filepath, 'rb'), 'image/jpeg'))
        ]

        # Add label information if a label was provided
        if label:
            metadata = {
                "version": 1,
                "type": "bounding-box-labels",
                "boundingBoxes": {
                    filename: [
                        {
                            "label": label,
                            "x": 0,
                            "y": 0,
                            "width": 640,
                            "height": 480
                        }
                    ]
                }
            }

            bbox_label = json.dumps(metadata, separators=(',', ':'))

            # Attach label file to upload
            files.append(('data', ('bounding_boxes.labels', bbox_label)))

        print(f"Uploading {filename} to Edge Impulse with label '{label}'")

        # Upload image to Edge Impulse
        response = requests.post(
            upload_url,
            headers=headers,
            files=files
        )

        print(f"Response Status: {response.status_code}")
        print(f"Response Body: {response.text}")

        # Return success message if upload worked
        if response.status_code == 200:
            return jsonify({
                'status': 'success',
                'message': f'Image uploaded successfully to {mode} mode',
                'response': response.json() if response.text else {},
                'image_url': image_url
            })

        # Return error if upload failed
        else:
            return jsonify({
                'status': 'error',
                'message': f'Upload failed: {response.text}'
            })

    except Exception as e:
        print(f"Error: {str(e)}")

        return jsonify({
            'status': 'error',
            'message': str(e)
        })

# Route for displaying saved images
@app.route('/static/<filename>')
def serve_image(filename):
    return send_from_directory(image_dir, filename)

# Start the Flask web server
if __name__ == '__main__':
    app.run(debug=True)
