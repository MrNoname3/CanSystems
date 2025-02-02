import json
import os
import time
import threading
import zlib
import base64
import sys
from tqdm import tqdm
import subprocess
import signal

try:
    import paho.mqtt.client as mqtt
except ModuleNotFoundError:
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
mqtt_server_name = mqtt_credentials['mqttServerUrl']
mqtt_server_port = mqtt_credentials['mqttServerPort']
mqtt_ca_cert = os.path.join(data_dir, 'mosq-ca.crt')
mqtt_client_id = 'Python_OTA'
mqtt_ota_send_topic = 'iot/stod/fcf5c401bd83/common'
mqtt_ota_receive_topic = 'iot/dtos/fcf5c401bd83/common'

# Set the path to firmware.bin
firmware_path_bin = os.path.join(current_dir, '.pio/build/project_esp32_can/firmware.bin')

# Callback function on connecting to MQTT broker
def on_connect(client, userdata, flags, rc):
    print(f"MQTT broker connection: {'SUCCESS' if rc == 0 else f'FAILED Result code {rc}'}")
    client.subscribe(mqtt_ota_receive_topic)
    send_fw_thread = threading.Thread(target=send_fw, daemon=False)
    send_fw_thread.start()

# Calculate CRC32 of the firmware file
def calculate_crc32(file_path):
    crc = 0
    with open(file_path, 'rb') as f:
        while chunk := f.read(4096):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFFFFFF

def get_fw_id(file_path):
    begin_of_id = b"project_"
    with open(file_path, 'rb') as file:
        data = file.read()
    start_index = data.find(begin_of_id)
    if start_index != -1:
        end_index = data.find(b'\0', start_index + len(begin_of_id))
        identifier = data[start_index:end_index].decode('utf-8')
        print(f"ID of bin file: \"{identifier}\"")
        return True, identifier
    print("ID not found for the file!")
    return False, ""

# Function to send firmware pieces
def send_fw():
    piece_size = 336
    piece_number = 0
    fw_size = os.path.getsize(firmware_path_bin)
    crc32_total = calculate_crc32(firmware_path_bin)
    success, identifier = get_fw_id(firmware_path_bin)
    if not success:
        exit_program()

    start_message = {"cmd": 2, "fileSize": fw_size, "crc32": crc32_total, "binId": identifier}
    client.publish(mqtt_ota_send_topic, json.dumps(start_message))
    print(f"Starting OTA! FW size: {fw_size}, CRC32: {crc32_total} ", end="")
    if not wait_for_ack(2):
        print("-> ERR")
        exit_program()
    print("-> OK")

    remaining_bytes = fw_size
    with open(firmware_path_bin, 'rb') as fw_file, tqdm(total=fw_size, desc="Sending Firmware", unit="B", unit_scale=True) as progress_bar:
        while remaining_bytes:
            read_size = min(remaining_bytes, piece_size)
            data = fw_file.read(read_size)
            piece_message = {"cmd": 3, "piece": piece_number, "data": base64.b64encode(data).decode('utf-8')}
            client.publish(mqtt_ota_send_topic, json.dumps(piece_message))
            if not wait_for_ack(3):
                print("NACK or timeout occurred during FW piece transmission!")
                exit_program()
            piece_number += 1
            remaining_bytes -= read_size
            progress_bar.update(read_size)
    
    check_message = {"cmd": 4}
    client.publish(mqtt_ota_send_topic, json.dumps(check_message))
    print("Checking FW ", end="")
    if not wait_for_ack(4):
        print("-> ERR")
        exit_program()
    print("-> OK")
    print("Upload done, exiting...")
    exit_program()

def exit_program():
    client.disconnect()
    client.loop_stop()
    sys.exit()

def signal_handler(sig, frame):
    exit_program()

command_status = {2: False, 3: False, 4: False}

def on_message(client, userdata, msg):
    message = json.loads(msg.payload)
    command_status[message["cmd"]] = message["type"] != 0

def wait_for_ack(cmd):
    timeout, start_time = 25, time.time()
    while not command_status[cmd]:
        if time.time() - start_time > timeout:
            return False
        time.sleep(0.05)
    command_status[cmd] = False
    return True

signal.signal(signal.SIGINT, signal_handler)
client = mqtt.Client(client_id=mqtt_client_id)
client.username_pw_set(username=mqtt_username, password=mqtt_password)
client.tls_set(ca_certs=mqtt_ca_cert)
client.on_connect = on_connect
client.on_message = on_message
client.connect(mqtt_server_name, mqtt_server_port, 60)
client.loop_forever()