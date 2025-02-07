import json
import os
import time
import zlib
import base64
import sys
from tqdm import tqdm
import subprocess
import signal
import enum

try:
    import paho.mqtt.client as mqtt
except ModuleNotFoundError:
    print("Paho MQTT library not found. Installing...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "paho-mqtt"])
    import paho.mqtt.client as mqtt

# Get the directory of the current script and move one folder back
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)

data_dir = os.path.join(parent_dir, 'data', 'config')
server_json_path = os.path.join(data_dir, 'server.json')
with open(server_json_path, 'r') as file:
    mqtt_credentials = json.load(file)

mqtt_username = mqtt_credentials['mqttUserName']
mqtt_password = mqtt_credentials['mqttPassword']
mqtt_server_name = mqtt_credentials['mqttServerUrl']
mqtt_server_port = mqtt_credentials['mqttServerPort']
mqtt_ca_cert = os.path.join(data_dir, 'mosq-ca.crt')
mqtt_client_id = 'Python_OTA'

mac_address = "fcf5c401bd83"
project_name = "project_esp32_can"

mqtt_ota_send_topic = f'iot/stod/{mac_address}/common'
mqtt_ota_receive_topic = f'iot/dtos/{mac_address}/common'

firmware_path_bin = os.path.join(parent_dir, f'.pio/build/{project_name}/firmware.bin')

# Define states for the firmware update process
class OTAState(enum.Enum):
    IDLE = 0
    WAIT_START_ACK = 1
    SENDING_FW = 2
    WAIT_PIECE_ACK = 3
    WAIT_CHECK_ACK = 4
    DONE = 5
    ERROR = 6

state = OTAState.IDLE
piece_number = 0
fw_size = 0
remaining_bytes = 0
crc32_total = 0
fw_file = None
progress_bar = None
timer_start = 0
piece_size = 100

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

def on_connect(client, userdata, flags, rc):
    global state, fw_size, crc32_total, fw_file, remaining_bytes, timer_start
    print(f"MQTT broker connection: {'SUCCESS' if rc == 0 else f'FAILED Result code {rc}'}")
    client.subscribe(mqtt_ota_receive_topic)
    
    fw_size = os.path.getsize(firmware_path_bin)
    crc32_total = calculate_crc32(firmware_path_bin)
    success, identifier = get_fw_id(firmware_path_bin)
    if not success:
        state = OTAState.ERROR
        return
    
    start_message = {"fileSize": fw_size, "crc32": crc32_total, "binId": identifier}
    client.publish(mqtt_ota_send_topic, json.dumps(start_message))
    print(f"Starting OTA! FW size: {fw_size}, CRC32: {crc32_total}")
    
    state = OTAState.WAIT_START_ACK
    timer_start = time.time()

def on_message(client, userdata, msg):
    global state, piece_number, remaining_bytes, fw_file, timer_start, progress_bar
    message = json.loads(msg.payload)
    
    if "type" in message:
        ack = message["type"] != 0
        if state == OTAState.WAIT_START_ACK and ack:
            fw_file = open(firmware_path_bin, 'rb')
            remaining_bytes = fw_size
            piece_number = 0
            progress_bar = tqdm(total=fw_size, desc="Sending Firmware", unit="B", unit_scale=True)
            state = OTAState.SENDING_FW
        elif state == OTAState.WAIT_PIECE_ACK and ack:
            state = OTAState.SENDING_FW
        elif state == OTAState.WAIT_CHECK_ACK and ack:
            state = OTAState.DONE
            print("\nUpload done, exiting...")
            exit_program()
        else:
            state = OTAState.ERROR
            print("\nError occurred during OTA!")
            exit_program()

def process_state():
    global state, piece_number, remaining_bytes, fw_file, timer_start, progress_bar
    if state == OTAState.SENDING_FW and remaining_bytes > 0:
        read_size = min(remaining_bytes, piece_size)
        data = fw_file.read(read_size)
        piece_message = {"piece": piece_number, "data": base64.b64encode(data).decode('utf-8')}
        client.publish(mqtt_ota_send_topic, json.dumps(piece_message))
        state = OTAState.WAIT_PIECE_ACK
        timer_start = time.time()
        piece_number += 1
        remaining_bytes -= read_size
        progress_bar.update(read_size)
    elif state == OTAState.SENDING_FW and remaining_bytes == 0:
        state = OTAState.WAIT_CHECK_ACK
        timer_start = time.time()
    elif state in (OTAState.WAIT_START_ACK, OTAState.WAIT_PIECE_ACK, OTAState.WAIT_CHECK_ACK):
        if time.time() - timer_start > 25:
            state = OTAState.ERROR
            print("Timeout occurred!")
            exit_program()

def exit_program():
    client.disconnect()
    client.loop_stop()
    sys.exit()

def signal_handler(sig, frame):
    exit_program()

signal.signal(signal.SIGINT, signal_handler)
client = mqtt.Client(client_id=mqtt_client_id)
client.username_pw_set(username=mqtt_username, password=mqtt_password)
client.tls_set(ca_certs=mqtt_ca_cert)
client.on_connect = on_connect
client.on_message = on_message
client.connect(mqtt_server_name, mqtt_server_port, 60)

while True:
    client.loop(timeout=0.1)
    process_state()
