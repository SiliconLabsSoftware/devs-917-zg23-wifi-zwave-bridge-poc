import socket
import struct
import argparse
import time
import sys
import select

CHUNK_SIZE = 40
GET_CHUNK_TIMEOUT = 30  # Timeout for receiving GET_CHUNK (in seconds)
ACK_TIMEOUT = 5  # Timeout for receiving ACK (in seconds)
CRC_POLY = 0x1021
CRC_INIT_VALUE = 0x1D0F
IRRIGATION_VALVE_RUN = 0x0D
IRRIGATION_SYSTEM_SHUTOFF = 0x12

manufacturer_id = 0
firmware_id = 0x0402  # Example firmware ID, can be changed as needed
firmware_target = 0  # Example firmware target, can be changed as needed
hadware_version = 0x01  # Example hardware version, can be changed as needed

security_enabled = True  # Set to True if security is enabled

def checksum_test(data):
    #data = "7A 06 00 01 EB 17 A6 03 08 00 00 00 00 00 00 03 01 01 00 00 FA 06 06 FA 10 00 00 00 B2 7F 02 00 EB AA 44 F7 18 8F 5A 7E BD 60 D4 AA"
    data_bytes = bytes.fromhex(data)
    checksum = calculate_checksum(data_bytes)
    print(f"Checksum: {checksum:#04x}")

def send_udp_message(sock, message, ip, port):
    """
    Send a UDP message to the Controller using IPv6.
    """
    print(message.hex(" "))
    try:
        sock.sendto(message, (ip, port, 0, 0))  # IPv6 requires a tuple with 4 elements
    except Exception as ex:
        print(str(ex))


def receive_ack(sock):
    """
    Wait for an ACK message from the Controller.
    """
    # time.sleep(0.36)  # Small delay to allow the ACK to arrive
    # return True

    try:
        sock.settimeout(ACK_TIMEOUT)
        data, addr = sock.recvfrom(1024)  # Buffer size of 1024 bytes
        print(f"Received ACK from {addr}: {data.hex()}")
        # 23024050fc00000084
        if len(data) < 8 or data[0:3] != b'\x23\x02\x40':
            print("Invalid ACK received.")
            return None
        return data
    except socket.timeout:
        print("ACK timeout. No response received.")
        return None

def receive_get_chunk(sock):
    """
    Wait for an ACK message from the Controller.
    """
    # time.sleep(0.36)  # Small delay to allow the ACK to arrive
    # return True

    while True:
        try:
            sock.settimeout(GET_CHUNK_TIMEOUT)
            data, addr = sock.recvfrom(1024)  # Buffer size of 1024 bytes
            print(f"Received ACK from {addr}: {data.hex()}")
            # 230200c001000005840200007a05050006
            # check 5 byte cuoi
            id1 = data[-1]
            id2 = data[-2]
            size = data[-3]
            cmd = data[-4]
            clas = data[-5]
            print(f"Received: size={size}, id1={id1}, id2={id2}, cmd={cmd}, clas={clas}")
            if (clas == 0x7A and cmd == 0x05):
                return [size, ((id2 << 8) + id1)]

            continue
        except socket.timeout:
            print("ACK timeout. No response received.")
            return [0, 0]

def zip_encapsulate(payload, seq_no=1):
    # Z/IP header: | 0x23 | 0x02 | flags0 | flags1 | seqNo | sEp | dEp |
    # flags0: 0x80 (ACK request), flags1: 0x50 (S0 encryption default)
    flags0 = 0x00  # Default flags for Z/IP
    flags1 = 0x40  # Default flags for Z/IP
    if security_enabled:
        flags1 = 0x50
    zip_header = struct.pack("!BBBBBBB", 0x23, 0x02, flags0, flags1, seq_no, 0, 0)
    return zip_header + payload

def create_firmware_update_report_v1(report_number, data, is_last_chunk):
    # 0x7A 0x06 (Firmware Update Meta Data Report)
    # Properties1: bit0 = 1 if last report, else 0
    properties1 = 0x80 if is_last_chunk else 0x00
    report_number1 = ((report_number >> 8) & 0x7F) | properties1
    report_number2 = report_number & 0xFF
    zip = struct.pack("!BBBB", 0x7A, 0x06, report_number1, report_number2) + data
    # Calculate checksum and append it
    checksum = calculate_checksum(zip)
    zip += struct.pack(">H", checksum)
    return zip


def create_firmware_update_get(manufacturer_id, firmware_id, checksum):
    # 0x7A 0x05 (Firmware Update Meta Data Get)
    return struct.pack(">BBHHH", 0x7A, 0x05, manufacturer_id, firmware_id, checksum)

def create_firmware_active_set(manufacturer_id, firmware_id, checksum, fw_target=0, hadware_version=0x01):
    # 0x7A 0x08 (Firmware Update Meta Data Get)
    return struct.pack(">BBHHHBB", 0x7A, 0x08, manufacturer_id, firmware_id, checksum, fw_target, hadware_version)

def create_firmware_update_report(report_number, data):
    # 0x7A 0x07 (Firmware Update Meta Data Report)
    properties1 = ((report_number >> 8) & 0x7F)
    report_number2 = report_number & 0xFF
    return struct.pack("!BBBB", 0x7A, 0x07, properties1, report_number2) + data


def create_firmware_update_status(status, wait_time):
    # 0x7A 0x08 (Firmware Update Meta Data Status Report)
    return struct.pack("<BBBB", 0x7A, 0x08, status, wait_time)


def create_firmware_md_get():
    # 0x7A 0x01 (Firmware MD Get)
    return struct.pack("!BB", 0x7A, 0x01)


def create_version_get():
    # 0x86 0x11 (Version Get)
    return struct.pack("!BB", 0x86, 0x11)


def create_binary_switch_set(on):
    # 0x25 0x01 <value>
    return struct.pack("!BBB", 0x25, 0x01, 0xFF if on else 0x00)


def create_irrigation_run(on, duration):
    duration1 = duration & 0xFF         # Low byte
    duration2 = (duration >> 8) & 0xFF  # High byte
    return struct.pack("<BBBBBB", 0x6B, IRRIGATION_VALVE_RUN, 0, 0, duration2, duration1)

def create_irrigation_shutoff(on, duration):
    duration1 = duration & 0xFF         # Low byte
    duration2 = (duration >> 8) & 0xFF  # High byte
    return struct.pack("<BBBBBB", 0x6B, IRRIGATION_SYSTEM_SHUTOFF, 0, 0, duration2, duration1)


def zgw_crc16(crc16, data):
    for crc_data in data:
        for bitmask in [0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01]:
            new_bit = ((crc_data & bitmask) != 0) ^ ((crc16 & 0x8000) != 0)
            crc16 = (crc16 << 1) & 0xFFFF
            if new_bit:
                crc16 ^= CRC_POLY
    return crc16


def calculate_checksum(data):
    # Use ZGW CRC16 with initial value CRC_INIT_VALUE
    return zgw_crc16(CRC_INIT_VALUE, data)


def make_node_ip(node_id):
    return f"fd00:bbbb:1::{format(node_id, 'x')}"


def send_update_get(sock, node_ip, port, manufacturer_id, firmware_id, checksum):
    payload = create_firmware_update_get(manufacturer_id, firmware_id, checksum)
    zip_pkt = zip_encapsulate(payload, seq_no=1)
    print("Sending Firmware Update Meta Data Get...")

    send_udp_message(sock, zip_pkt, node_ip, port)
    print("Packet sent.")

def send_update_active_set(sock, node_ip, port, manufacturer_id, firmware_id, checksum, total_chunks):
    payload = create_firmware_active_set(manufacturer_id, firmware_id, checksum, fw_target=firmware_target, hadware_version=hadware_version)
    zip_pkt = zip_encapsulate(payload, seq_no=(total_chunks + 2) % 256)
    print("Sending Firmware Update Active Set...")

    send_udp_message(sock, zip_pkt, node_ip, port)
    print("Packet sent.")


def send_update_status(sock, node_ip, port, total_chunks):
    print("Sending firmware verification request...")
    status = 0x00  # Success
    wait_time = 5
    payload = create_firmware_update_status(status, wait_time)
    zip_pkt = zip_encapsulate(payload, seq_no=(total_chunks + 2) % 256)
    send_udp_message(sock, zip_pkt, node_ip, port)
    print("Status packet sent.")


def send_firmware_md_get(sock, node_ip, port):
    payload = create_firmware_md_get()
    zip_pkt = zip_encapsulate(payload, seq_no=10)
    print("Sending Firmware MD Get (firmware id)...")
    send_udp_message(sock, zip_pkt, node_ip, port)
    print("Packet sent.")


def send_version_get(sock, node_ip, port):
    payload = create_version_get()
    zip_pkt = zip_encapsulate(payload, seq_no=11)
    print("Sending Version Get (application version)...")
    send_udp_message(sock, zip_pkt, node_ip, port)
    print("Packet sent.")


def send_binary_switch(sock, node_ip, port):
    onoff = input("Enter 1 for ON, 0 for OFF: ").strip()
    if onoff == "1":
        payload = create_binary_switch_set(True)
        desc = "Binary Switch ON"
        seq_no = 20
    elif onoff == "0":
        payload = create_binary_switch_set(False)
        desc = "Binary Switch OFF"
        seq_no = 21
    else:
        print("Invalid ON/OFF selection.")
        return
    zip_pkt = zip_encapsulate(payload, seq_no=seq_no)
    print(f"Sending {desc}...")
    send_udp_message(sock, zip_pkt, node_ip, port)
    print("Packet sent.")


def send_irrigation(sock, node_ip, port, state, duration):
    if state == "run":
        payload = create_irrigation_run(1, duration)
        desc = "Irrigation RUN"
        seq_no = 30
    elif run == "shutoff":
        payload = create_irrigation_shutoff(1, duration)
        desc = "Irrigation SHUTOFF"
        seq_no = 31
    else:
        print("Invalid irrigation selection.")
        return
    zip_pkt = zip_encapsulate(payload, seq_no=seq_no)
    print(f"Sending {desc}...")
    send_udp_message(sock, zip_pkt, node_ip, port)
    print("Packet sent.")


def create_firmware_update_md_request_get(manufacturer_id, firmware_id, checksum=0, fw_target=0):
    # 0x7A 0x03 (Firmware Update Meta Data Request Get)
    return struct.pack(">BBHHHBBBBB", 0x7A, 0x03, manufacturer_id, firmware_id, checksum, fw_target, 0, 0x28, 0, 1)


def send_firmware_update_md_request_get(sock, node_ip, port, manufacturer_id, firmware_id, checksum=0, fw_target=0):
    payload = create_firmware_update_md_request_get(manufacturer_id, firmware_id, checksum, fw_target)
    zip_pkt = zip_encapsulate(payload, seq_no=1)
    print("Sending Firmware Update Meta Data Request Get (0x7A 0x03)...")
    send_udp_message(sock, zip_pkt, node_ip, port)
    print("Packet sent. Waiting for Request Report (0x7A 0x04)...")
    # time.sleep(1.5)  # Wait for the node to process the request
    return True  # For now, we assume the node accepts the request
    # ack = receive_ack(sock)
    # if ack and b'\x7A\x04' in ack:
    #     print("Received Firmware Update Meta Data Request Report (0x7A 0x04).")
    #     return True
    # else:
    #     print("Did not receive expected Request Report. Aborting OTA.")
    #     return False


def ota_update(sock, node_id, firmware_file):
    controller_port = 4123
    node_ip = make_node_ip(node_id)
    print(f"Target node IPv6 address: {node_ip}")

    with open(firmware_file, "rb") as f:
        firmware = f.read()

        checksum = calculate_checksum(firmware)
        print(f"Firmware checksum: {checksum:#04x}")

        if not send_firmware_update_md_request_get(sock, node_ip, controller_port, manufacturer_id, firmware_id, checksum, firmware_target):
            print("Node did not accept firmware update request.")
            return

        print("Starting firmware data transmission...")
        total_chunks = (len(firmware) + CHUNK_SIZE - 1) // CHUNK_SIZE

        chund_sent = 0
        is_last_chunk = False
        while (is_last_chunk is False):
            ch_size, idex = receive_get_chunk(sock)

            if ch_size == 0:
                print("No more chunks to send or an error occurred. Exiting...")
                return

            for i in range(idex, idex + ch_size):
                print(f"sending chunk {i}/{total_chunks}...")
                chunk = firmware[(i-1) * CHUNK_SIZE:(i) * CHUNK_SIZE]
                is_last_chunk = (i >= total_chunks)
                payload = create_firmware_update_report_v1(i, chunk, is_last_chunk)
                seq_no = (i + 2) % 256
                zip_pkt = zip_encapsulate(payload, seq_no=seq_no)
                send_udp_message(sock, zip_pkt, node_ip, controller_port)
                time.sleep(0.05)
                print(f" Sent chunk {i}/{total_chunks}")
                if is_last_chunk:
                    print("Last chunk sent.")
                    break
            chund_sent += ch_size

        print("OTA Update completed!")


def main():
    controller_port = 4123
    node_id = None
    firmware_file = None

    with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(('::', controller_port))

        while True:
            print("\nMenu:")
            print("1. Set Node ID")
            print("2. Get Firmware ID (Firmware MD Get)")
            print("3. Get Application Version (Version Get)")
            print("4. Turn ON/OFF Binary Switch")
            print("5. Run/Shutoff Irrigation")
            print("t. Test Checksum")
            print("s. Set security enabled (True/False)")
            print("x. Exit")
            print(f"Current Node ID: {node_id}")
            choice = input("Select option (1-9/x): ").strip().lower()

            if choice == "x":
                print("Exiting.")
                break
            elif choice == "1":
                try:
                    node_id = int(input("Enter Node ID (decimal): ").strip())
                except ValueError:
                    print("Invalid Node ID.")
            elif choice == "s":
                security_input = input("Set security enabled (True/False): ").strip().lower()
                if security_input in ["true", "1"]:
                    global security_enabled
                    security_enabled = True
                    print("Security enabled.")
                elif security_input in ["false", "0"]:
                    security_enabled = False
                    print("Security disabled.")
                else:
                    print("Invalid input. Please enter True or False.")
            elif choice == "2":
                if node_id is None:
                    print("Please set Node ID first.")
                    continue
                node_ip = make_node_ip(node_id)
                send_firmware_md_get(sock, node_ip, controller_port)
            elif choice == "3":
                if node_id is None:
                    print("Please set Node ID first.")
                    continue
                node_ip = make_node_ip(node_id)
                send_version_get(sock, node_ip, controller_port)
            elif choice == "4":
                if node_id is None:
                    print("Please set Node ID first.")
                    continue
                node_ip = make_node_ip(node_id)
                send_binary_switch(sock, node_ip, controller_port)
            elif choice == "5":
                state, duration = input("Enter run/shutoff duration: ").split()
                if node_id is None:
                    print("Please set Node ID first.")
                    continue
                node_ip = make_node_ip(node_id)
                send_irrigation(sock, node_ip, controller_port, state, int(duration))
            elif choice == "t":
                data = input("Enter data to calculate checksum (hex format): ").strip()
                checksum_test(data)
            else:
                print("Invalid option.")


if __name__ == "__main__":
    main()
