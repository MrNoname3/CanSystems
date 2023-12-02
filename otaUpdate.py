import json
import os
import time  # Import the time module
import binascii
import threading

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
mqtt_ota_topic = 'iot/stod/testmac/common'

# Callback function on connecting to MQTT broker
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code " + str(rc))
    # Start a thread to send firmware pieces
    send_fw_thread = threading.Thread(target=send_fw, daemon=True)
    send_fw_thread.start()

# Calculate CRC32 of the firmware file
def calculate_crc32(file_path):
    with open(file_path, 'rb') as file:
        content = file.read()
        return binascii.crc32(content) & 0xFFFFFFFF

# Set the path to firmware.bin
firmware_path = os.path.join(current_dir, '.pio/build/d1_mini/firmware.bin')

# Function to send firmware pieces
def send_fw():
    piece_size = 100
    piece_number = 0  # Start from 0

    fw_size = os.path.getsize(firmware_path)
    crc32 = calculate_crc32(firmware_path)

    # Start message
    start_message = {
        "command": 2,
        "fwSize": fw_size,
        "crc32": crc32
    }
    client.publish(mqtt_ota_topic, json.dumps(start_message))
    time.sleep(0.1)  # 100ms delay

    # Send firmware in pieces
    with open(firmware_path, 'rb') as fw_file:
        while True:
            data = fw_file.read(piece_size)
            if not data:
                break

            piece_message = {
                "command": 3,
                "pieceNumber": piece_number,
                "fwData": list(data)
            }
            client.publish(mqtt_ota_topic, json.dumps(piece_message))
            piece_number += 1
            time.sleep(0.1)  # 100ms delay

        # If there are remaining bytes (< 100), send as the final piece
        if data:
            piece_message = {
                "command": 3,
                "pieceNumber": piece_number,
                "fwData": list(data)
            }
            client.publish(mqtt_ota_topic, json.dumps(piece_message))

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

