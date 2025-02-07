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

mac_address = "bcddc2b622c9"
project_name = "project_esp8266_smoke"

mqtt_ota_send_topic = f'iot/stod/{mac_address}/common'
mqtt_ota_receive_topic = f'iot/dtos/{mac_address}/common'

firmware_path_bin = os.path.join(parent_dir, f'.pio/build/{project_name}/firmware.bin')

class OTAState(enum.Enum):
    IDLE = 0
    WAIT_START_ACK = 1
    SENDING_FW = 2
    WAIT_PIECE_ACK = 3
    WAIT_CHECK_ACK = 4
    DONE = 5
    ERROR = 6

class OTAUpdater:
    def __init__(self):
        self.state = OTAState.IDLE
        self.piece_number = 0
        self.fw_size = 0
        self.remaining_bytes = 0
        self.crc32_total = 0
        self.fw_file = None
        self.progress_bar = None
        self.timer_start = 0
        self.piece_size = 100

        self.client = mqtt.Client(client_id=mqtt_client_id)
        self.client.username_pw_set(username=mqtt_username, password=mqtt_password)
        self.client.tls_set(ca_certs=mqtt_ca_cert)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.connect(mqtt_server_name, mqtt_server_port, 60)

    def calculate_crc32(self, file_path):
        crc = 0
        with open(file_path, 'rb') as f:
            while chunk := f.read(4096):
                crc = zlib.crc32(chunk, crc)
        return crc & 0xFFFFFFFF

    def get_fw_id(self, file_path):
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

    def on_connect(self, client, userdata, flags, rc):
        print(f"MQTT broker connection: {'SUCCESS' if rc == 0 else f'FAILED Result code {rc}'}")
        client.subscribe(mqtt_ota_receive_topic)
        
        self.fw_size = os.path.getsize(firmware_path_bin)
        self.crc32_total = self.calculate_crc32(firmware_path_bin)
        success, identifier = self.get_fw_id(firmware_path_bin)
        if not success:
            self.state = OTAState.ERROR
            return
        
        start_message = {"fileSize": self.fw_size, "crc32": self.crc32_total, "binId": identifier}
        client.publish(mqtt_ota_send_topic, json.dumps(start_message))
        print(f"Starting OTA! FW size: {self.fw_size}, CRC32: {self.crc32_total}")
        
        self.state = OTAState.WAIT_START_ACK
        self.timer_start = time.time()

    def on_message(self, client, userdata, msg):
        message = json.loads(msg.payload)
        
        if "type" in message:
            ack = message["type"] != 0
            if self.state == OTAState.WAIT_START_ACK and ack:
                self.fw_file = open(firmware_path_bin, 'rb')
                self.remaining_bytes = self.fw_size
                self.piece_number = 0
                self.progress_bar = tqdm(total=self.fw_size, desc="Sending Firmware", unit="B", unit_scale=True)
                self.state = OTAState.SENDING_FW
            elif self.state == OTAState.WAIT_PIECE_ACK and ack:
                self.state = OTAState.SENDING_FW
            elif self.state == OTAState.WAIT_CHECK_ACK and ack:
                self.state = OTAState.DONE
                print("\nUpload done, exiting...")
                self.exit_program()
            else:
                self.state = OTAState.ERROR
                print("\nError occurred during OTA!")
                self.exit_program()

    def process_state(self):
        if self.state == OTAState.SENDING_FW and self.remaining_bytes > 0:
            read_size = min(self.remaining_bytes, self.piece_size)
            data = self.fw_file.read(read_size)
            piece_message = {"piece": self.piece_number, "data": base64.b64encode(data).decode('utf-8')}
            self.client.publish(mqtt_ota_send_topic, json.dumps(piece_message))
            self.state = OTAState.WAIT_PIECE_ACK
            self.timer_start = time.time()
            self.piece_number += 1
            self.remaining_bytes -= read_size
            self.progress_bar.update(read_size)
        elif self.state == OTAState.SENDING_FW and self.remaining_bytes == 0:
            self.state = OTAState.WAIT_CHECK_ACK
            self.timer_start = time.time()
        elif self.state in (OTAState.WAIT_START_ACK, OTAState.WAIT_PIECE_ACK, OTAState.WAIT_CHECK_ACK):
            if time.time() - self.timer_start > 25:
                self.state = OTAState.ERROR
                print("Timeout occurred!")
                self.exit_program()

    def exit_program(self):
        self.client.disconnect()
        self.client.loop_stop()
        sys.exit()

    def run(self):
        while True:
            self.client.loop(timeout=0.1)
            self.process_state()

if __name__ == "__main__":
    ota_updater = OTAUpdater()
    signal.signal(signal.SIGINT, lambda sig, frame: ota_updater.exit_program())
    ota_updater.run()