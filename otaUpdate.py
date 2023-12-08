import json
import os
import time  # Import the time module
import binascii
import threading
import zlib
import base64

try:
    import paho.mqtt.client as mqtt
except ModuleNotFoundError:
    import subprocess
    import sys

    print("Paho MQTT library not found. Installing...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "paho-mqtt"])
    import paho.mqtt.client as mqtt

# Get the directory of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))

# Navigate to the data directory
data_dir = os.path.join(current_dir, 'data', 'config')

# Read MQTT credentials from server.json
server_json_path = os.path.join(data_dir, 'server.json')
with open(server_json_path, 'r') as file:
    mqtt_credentials = json.load(file)

# MQTT configuration
mqtt_username = mqtt_credentials['mqttUserName']
mqtt_password = mqtt_credentials['mqttPassword']
mqtt_server_name = mqtt_credentials['mqttServerName']
mqtt_server_port = mqtt_credentials['mqttServerPort']
mqtt_ca_cert = os.path.join(data_dir, 'mosq-ca.crt')
mqtt_client_id = 'Python_OTA'
mqtt_ota_topic = 'iot/stod/40f520286e69/common'

# Callback function on connecting to MQTT broker
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code " + str(rc))
    # Start a thread to send firmware pieces
    send_fw_thread = threading.Thread(target=send_fw, daemon=True)
    send_fw_thread.start()

# Calculate CRC32 of the firmware file
def calculate_crc32(file_path):
    crc = 0  # Initial CRC value

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(1)
            if not data:
                break
            crc = zlib.crc32(data, crc)

    return crc & 0xFFFFFFFF


# Set the path to firmware.bin
firmware_path = os.path.join(current_dir, '.pio/build/d1_mini/firmware.bin')

# Function to send firmware pieces
def send_fw():
    piece_size = 288
    piece_number = 0  # Start from 0

    fw_size = os.path.getsize(firmware_path)
    crc32 = calculate_crc32(firmware_path)

    # Start message
    start_message = {
        "cmd": 2,
        "fwSize": fw_size,
        "crc32": crc32
    }
    client.publish(mqtt_ota_topic, json.dumps(start_message))
    print("Start:", json.dumps(start_message))
    time.sleep(0.2)  # 100ms delay

    # Send firmware in pieces
    remaining_bytes = fw_size
    with open(firmware_path, 'rb') as fw_file:
        while remaining_bytes != 0:
            read_size = min(remaining_bytes, piece_size)  # Read up to 100 bytes at a time
            data = fw_file.read(read_size)

            piece_message = {
                "cmd": 3,
                "piece": piece_number,
                "size": read_size,
                "data": base64.b64encode(data).decode('utf-8')  # Convert bytes to base64-encoded string
            }
            print("Piece:", json.dumps(piece_message))  # Print the JSON message
            client.publish(mqtt_ota_topic, json.dumps(piece_message))
            piece_number += 1
            remaining_bytes -= read_size
            time.sleep(0.2)  # 50ms delay
    end_message = {
        "cmd": 4
    }
    client.publish(mqtt_ota_topic, json.dumps(end_message))
    print("End:", json.dumps(end_message))

    print("Upload done, exiting...")
    client.disconnect()
    sys.exit()


# Run MQTT loop
client = mqtt.Client(client_id=mqtt_client_id)
client.username_pw_set(username=mqtt_username, password=mqtt_password)
client.tls_set(ca_certs=mqtt_ca_cert)
client.on_connect = on_connect
client.connect(mqtt_server_name, mqtt_server_port, 60)
client.loop_forever()

