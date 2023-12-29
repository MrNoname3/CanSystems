import json
import os
import time
import threading
import zlib
import base64
import sys
from tqdm import tqdm
import subprocess

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
mqtt_ota_send_topic = 'iot/stod/40f520286e69/common'
mqtt_ota_receive_topic = 'iot/dtos/40f520286e69/common'

# Set the path to firmware.bin
firmware_path_bin = os.path.join(current_dir, '.pio/build/d1_mini/firmware.bin')
firmware_path_gz = os.path.join(current_dir, '.pio/build/d1_mini/firmware.bin.gz')

# Callback function on connecting to MQTT broker
def on_connect(client, userdata, flags, rc):
    print(f"MQTT broker connection: {'SUCCESS' if rc == 0 else f'FAILED Result code {rc}'}")

    comp_result = compress_with_7z(firmware_path_bin, firmware_path_gz)
    if comp_result != 0:
        exit_program()

    # Subscribe to the OTA acknowledgment topic here
    client.subscribe(mqtt_ota_receive_topic)
    # Start a thread to send firmware pieces
    send_fw_thread = threading.Thread(target=send_fw, daemon=False)
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

def compress_with_7z(bin_path, gz_path):
    # Run 7z command to compress firmware.bin to firmware.bin.gz
    command = [ '7z', 'a', '-tgzip', '-mx=9', gz_path, bin_path ]

    try:
        # Run the command
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output, error = process.communicate()
        returncode = process.returncode
        
        # Check the return code and output
        if returncode == 0:
            print("Compression successful!")
            print("Output:", output.decode())
        else:
            print("Compression failed!")
            print("Error:", error.decode())
        
        return returncode
    except Exception as e:
        print("Error:", str(e))
        return 1  # Return a non-zero code to indicate failure

# Function to send firmware pieces
def send_fw():
    piece_size = 336
    piece_number = 0  # Start from 0
    fw_size = os.path.getsize(firmware_path_gz)
    crc32_total = calculate_crc32(firmware_path_gz)

    # Start message
    start_message = {
        "cmd": 2,
        "fwSize": fw_size,
        "crc32": crc32_total
    }
    client.publish(mqtt_ota_send_topic, json.dumps(start_message))
    print(f"Starting OTA! FW size: {fw_size}, CRC32: {crc32_total} ", end="")
    starting_result = wait_for_ack(2)
    print(f"-> {'OK' if starting_result else 'ERR'}")
    if starting_result == False:
        exit_program()

    # Send firmware in pieces
    remaining_bytes = fw_size
    with open(firmware_path_gz, 'rb') as fw_file:
        # Initialize tqdm progress bar
        progress_bar = tqdm(total=fw_size, desc="Sending Firmware", unit="B", unit_scale=True)
        while remaining_bytes != 0:
            read_size = min(remaining_bytes, piece_size)
            data = fw_file.read(read_size)
            piece_message = {
                "cmd": 3,
                "piece": piece_number,
                "data": base64.b64encode(data).decode('utf-8')
            }
            client.publish(mqtt_ota_send_topic, json.dumps(piece_message))
            sending_piece_result = wait_for_ack(3)
            if sending_piece_result == False:
                progress_bar.close()
                print("NACK or timeout occured during FW piece transmission!")
                exit_program()
            piece_number += 1
            remaining_bytes -= read_size
            progress_bar.update(read_size)      # Update tqdm progress bar
        progress_bar.close()                    # Close tqdm progress bar

    check_message = {
        "cmd": 4
    }
    client.publish(mqtt_ota_send_topic, json.dumps(check_message))
    print("Checking FW ", end="")
    fw_check_result = wait_for_ack(4)
    print(f"-> {'OK' if fw_check_result else 'ERR'}")
    if fw_check_result == False:
        exit_program()
    else:
        print("Upload done, exiting...")
    exit_program()

def exit_program():
    client.disconnect()
    client.loop_stop()
    sys.exit()

# Define a global variable or use a queue to manage commands and their corresponding ACK/NACK status
command_status = {
    2: False,  # Start command
    3: False,  # Piece command
    4: False   # End command
}

# Callback function when a message is received
def on_message(client, userdata, msg):
    message = json.loads(msg.payload)
    if message["type"] == 1 or message["type"] == 0:
        cmd = message["cmd"]
        command_status[cmd] = True  # Update the status based on the received ACK/NACK

# Function to wait for ACK/NACK for a particular command with a timeout
def wait_for_ack(cmd):
    timeout = 10  # timeout in seconds
    start_time = time.time()

    while not command_status[cmd]:
        elapsed_time = time.time() - start_time
        if elapsed_time > timeout:
            return False
        time.sleep(0.05)  # Small delay to avoid excessive CPU usage on the MCU

    if command_status[cmd]:
        command_status[cmd] = False
        return True
    else:
        return False

# Run MQTT loop
client = mqtt.Client(client_id=mqtt_client_id)
client.username_pw_set(username=mqtt_username, password=mqtt_password)
client.tls_set(ca_certs=mqtt_ca_cert)
client.on_connect = on_connect
client.on_message = on_message
client.connect(mqtt_server_name, mqtt_server_port, 60)
client.loop_forever()

