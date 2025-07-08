import socket
import ssl
import threading
import struct
import os
import hashlib
import sys

# Global variables to store connected clients
clients = {}
lock = threading.Lock()

COMMAND_CLASS_ZIP = 0x23

# Binary switch command class
COMMAND_ZIP_PACKET = 0x02
COMMAND_CLASS_SWITCH_BINARY = 0x25
SWITCH_BINARY_SET = 0x01

# Irrigation command class
COMMAND_CLASS_IRRIGATION = 0x6B
IRRIGATION_VALVE_RUN = 0x0D
IRRIGATION_SYSTEM_SHUTOFF = 0x12

# Command codes for packet struct
ZIP_PACKET_COMMAND = 0x10
OTA_BRIDGE = 0x20
OTA_NCP = 0x30
OTA_NODE = 0x40

RPS_HEADER = 0x01
RPS_DATA = 0x00

CHK_SIZE = 0
IS_RECV = False

def recv_exact(conn, n):
    """Read exactly n bytes from conn, or return less if connection closes."""
    buf = b''
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            break
        buf += chunk
    return buf

# Function to handle each client connection
def handle_client(client_socket, client_address):
    with lock:
        clients[client_address] = client_socket
    print(f"Client connected: {client_address}")

    try:
        while True:
            data = client_socket.recv(1024)
            if not data:
                break
            print(f"Received from {client_address} len {len(data)}: {data.decode()}")
            CHK_SIZE = 256
            IS_RECV = True

    except ConnectionResetError:
        print(f"Client disconnected: {client_address}")
    finally:
        with lock:
            del clients[client_address]
        client_socket.close()

# Function to list connected clients
def list_clients():
    with lock:
        if not clients:
            print("No clients connected.")
        else:
            print("Connected clients:")
            for idx, client in enumerate(clients.keys(), start=1):
                print(f"{idx}. {client}")

# Function to send data to a specific client
def send_data():
    list_clients()
    with lock:
        if not clients:
            return
        try:
            choice = int(input("Select client number to send data: ")) - 1
            if choice < 0 or choice >= len(clients):
                print("Invalid choice.")
                return
            client_address = list(clients.keys())[choice]
            message = input("Enter message to send: ")
            clients[client_address].sendall(message.encode())
            print(f"Message sent to {client_address}")
        except (ValueError, IndexError):
            print("Invalid input.")
        except BrokenPipeError:
            print("Failed to send message. Client may have disconnected.")

# Function to create a Z-Wave ZIP packet
def create_zip_packet(nodeid, command_class, command, value, sEndpoint, dEndpoint, enc):
    # Define the ZIP packet header format
    # Format: nodeid (2 bytes), cmdClass (1 byte), cmd (1 byte), flags0 (1 byte),
    #         flags1 (1 byte), seqNo (1 byte), sEndpoint (1 byte), dEndpoint (1 byte)
    zip_header_format = "<HBBBBBBB"  # Big-endian (>), little-endian (<): H=short, B=byte
    flags0 = 0x80  # Example flags
    flags1 = 0x40 | (0x10 if enc else 0x00)  # Example flags with encryption
    seqNo = 0x01  # Example sequence number

    # Create the header
    zip_header = struct.pack(
        zip_header_format,
        nodeid,
        0x23,  # COMMAND_CLASS_ZIP
        0x02,  # COMMAND_ZIP_PACKET
        flags0,
        flags1,
        seqNo,
        sEndpoint,
        dEndpoint,
    )

    # Create the payload
    if command_class == COMMAND_CLASS_SWITCH_BINARY:
        payload = struct.pack("BBBB", command_class, command, value, 0x00)
    elif command_class == COMMAND_CLASS_IRRIGATION:
        if command == IRRIGATION_VALVE_RUN:

            # duration is 16 bits, split into two bytes
            value1 = (value >> 8) & 0xFF  # high byte
            value2 = value & 0xFF         # low byte

            payload = struct.pack("BBBBBB", command_class, command, 0, 0, value1, value2)
        elif command == IRRIGATION_SYSTEM_SHUTOFF:
            payload = struct.pack("BBB", command_class, command, value)
        else:
            raise ValueError(f"Unsupported irrigation command: {command}")
    else:
        raise ValueError(f"Unsupported command class: {command_class}")

    # Combine header and payload
    zip_packet = struct.pack("B", ZIP_PACKET_COMMAND) + zip_header + payload
    return zip_packet

# Function to send data to a specific client
def send_binary_switch_data():
    list_clients()
    with lock:
        if not clients:
            return
        try:
            choice = int(input("Select client number to send data: ")) - 1
            if choice < 0 or choice >= len(clients):
                print("Invalid choice.")
                return
            client_address = list(clients.keys())[choice]
            nodeid, state, enc = input("Send command (nodeid on/off enc): ").split()

            value = 0
            if state == "on":
                value = 0xFF
            # Create the ZIP packet
            zip_packet = create_zip_packet(
                int(nodeid),
                COMMAND_CLASS_SWITCH_BINARY,
                SWITCH_BINARY_SET,
                value,
                0,
                0,
                int(enc)
            )

            clients[client_address].sendall(zip_packet)
            print(f"Message sent to {client_address}")
        except (ValueError, IndexError):
            print("Invalid input.")
        except BrokenPipeError:
            print("Failed to send message. Client may have disconnected.")

# Function to send irrigation data
def send_irrigation_data():
    list_clients()
    with lock:
        if not clients:
            return
        try:
            choice = int(input("Select client number to send data: ")) - 1
            if choice < 0 or choice >= len(clients):
                print("Invalid choice.")
                return
            client_address = list(clients.keys())[choice]
            nodeid, state, duration, enc = input("Send command (nodeid run/shutoff duration enc): ").split()

            if state == "run": temp_command = IRRIGATION_VALVE_RUN
            elif state == "shutoff": temp_command = IRRIGATION_SYSTEM_SHUTOFF
            else:
                print("Invalid state. Use 'run' or 'shutoff'.")
                return
            # Create the ZIP packet
            zip_packet = create_zip_packet(
                int(nodeid),
                COMMAND_CLASS_IRRIGATION,
                temp_command,
                int(duration),
                0,
                0,
                int(enc)
            )

            clients[client_address].sendall(zip_packet)
            print(f"Message sent to {client_address}")
        except (ValueError, IndexError):
            print("Invalid input.")
        except BrokenPipeError:
            print("Failed to send message. Client may have disconnected.")

def process_request_py(conn, fp):
    ctr = 0
    fp.seek(0)
    chunk = fp.read(64)
    length = len(chunk)
    # RPS_HEADER packet: [OTA_BRIDGE][RPS_HEADER][len_lo][len_hi][data...]
    data1 = struct.pack("<BH", RPS_HEADER, length) + chunk
    packet = struct.pack("B", OTA_BRIDGE) + data1
    print(f"length of first chunk=={length}")
    conn.sendall(packet)

    while True:
        try:
            print("waiting for recv")
            data = recv_exact(conn, 3)
            print(f"recv length == 0x{len(data):x}")
            if len(data) < 2:
                print("Connection closed or protocol error")
                return
            type = data[0]
            sz = data[1] | (data[2] << 8)
            print(f"size of data==0x{sz:x}")

            chunk = fp.read(sz)
            length = len(chunk)
            data1 = struct.pack("<BH", RPS_DATA, length) + chunk
            packet = struct.pack("B", OTA_BRIDGE) + data1

            if length < sz:
                print("reach end of file")
                conn.sendall(packet)
                # Send zero-length packet to indicate EOF
                data1 = struct.pack("<BH", RPS_DATA, 0)
                packet = struct.pack("B", OTA_BRIDGE) + data1
                conn.sendall(packet)
                return

            print(f"size of data1=={length}")
            conn.sendall(packet)

            print(f"Pkt sent no:{ctr+1}")
            ctr += 1
        except Exception as e:
            print(f"Error: {e}")
            return

# Example stubs for OTA Bridge and OTA NCP (to be implemented)
def ota_bridge():
    print("OTA Bridge selected.")
    list_clients()
    with lock:
        if not clients:
            print("No clients connected.")
            return
        try:
            choice = int(input("Select client number to send OTA: ")) - 1
            if choice < 0 or choice >= len(clients):
                print("Invalid choice.")
                return
            client_address = list(clients.keys())[choice]
            firmware_path = input("Enter firmware file path: ")
            try:
                fp = open(firmware_path, "rb")
            except Exception as e:
                print(f"Failed to open firmware file: {e}")
                return
            conn = clients[client_address]
            process_request_py(conn, fp)
            fp.close()
        except Exception as e:
            print(f"OTA Bridge error: {e}")

def process_ota_ncp(conn, fp):
    ctr = 0
    # calculate the size of the file.
    size = os.path.getsize(fp.name)
    print(f"size of file=={size}")
    # convert size to bytes array
    data = struct.pack("<I", size)
    # calculate the md5 of the file.
    md5 = hashlib.md5(fp.read()).hexdigest()
    print(f"md5 of file=={md5}, length=={len(md5)}")
    # convert md5 to bytes array
    data += bytes.fromhex(md5)

    length = len(data)
    # RPS_HEADER packet: [OTA_NCP][RPS_HEADER][len_lo][len_hi][data...]
    data1 = struct.pack("<BH", RPS_HEADER, length) + data
    packet = struct.pack("B", OTA_NCP) + data1
    print(f"length of first chunk=={length}")
    conn.sendall(packet)
    fp.seek(0)

    while True:
        try:
            print("waiting for recv")
            data = recv_exact(conn, 3)
            print(f"recv length == 0x{len(data):x}")
            if len(data) < 2:
                print("Connection closed or protocol error")
                return
            type = data[0]
            sz = data[1] | (data[2] << 8)
            print(f"size of data=={sz}")

            chunk = fp.read(sz)
            length = len(chunk)
            data1 = struct.pack("<BH", RPS_DATA, length) + chunk
            packet = struct.pack("B", OTA_NCP) + data1

            if length < sz:
                print("reach end of file")
                conn.sendall(packet)
                # Send zero-length packet to indicate EOF
                data1 = struct.pack("<BH", RPS_DATA, 0)
                packet = struct.pack("B", OTA_NCP) + data1
                conn.sendall(packet)
                return

            print(f"size of data1=={length}")
            conn.sendall(packet)

            print(f"Pkt sent no:{ctr+1}")
            ctr += 1
        except Exception as e:
            print(f"Error: {e}")
            return

def ota_ncp():
    print("OTA NCP selected.")
    list_clients()
    with lock:
        if not clients:
            print("No clients connected.")
            return
        try:
            choice = int(input("Select client number to send OTA: ")) - 1
            if choice < 0 or choice >= len(clients):
                print("Invalid choice.")
                return
            client_address = list(clients.keys())[choice]
            firmware_path = input("Enter firmware file path: ")
            try:
                fp = open(firmware_path, "rb")
            except Exception as e:
                print(f"Failed to open firmware file: {e}")
                return
            conn = clients[client_address]
            process_ota_ncp(conn, fp)
            fp.close()
        except Exception as e:
            print(f"OTA NCP error: {e}")


def process_ota_node(conn, fp, nodeid):
    ctr = 0
    # calculate the size of the file.
    size = os.path.getsize(fp.name)
    print(f"size of file=={size}")
    # convert size to bytes array
    data = struct.pack("<I", size)
    # calculate the md5 of the file.
    md5 = hashlib.md5(fp.read()).hexdigest()
    print(f"md5 of file=={md5}, length=={len(md5)}")
    # convert md5 to bytes array
    data += nodeid.to_bytes(2, 'big')
    data += bytes.fromhex(md5)

    length = len(data)
    # RPS_HEADER packet: [OTA_NCP][RPS_HEADER][len_lo][len_hi][data...]
    data1 = struct.pack("<BH", RPS_HEADER, length) + data
    packet = struct.pack("B", OTA_NODE) + data1
    print(f"length of first chunk=={length}")
    conn.sendall(packet)
    fp.seek(0)

    while True:
        try:
            print("waiting for recv")
            data = recv_exact(conn, 3)
            print(f"recv length == 0x{len(data):x}")
            if len(data) < 2:
                print("Connection closed or protocol error")
                return
            type = data[0]
            sz = data[1] | (data[2] << 8)
            print(f"size of data=={sz}")

            chunk = fp.read(sz)
            length = len(chunk)
            data1 = struct.pack("<BH", RPS_DATA, length) + chunk
            packet = struct.pack("B", OTA_NODE) + data1

            if length < sz:
                print("reach end of file")
                conn.sendall(packet)
                # Send zero-length packet to indicate EOF
                data1 = struct.pack("<BH", RPS_DATA, 0)
                packet = struct.pack("B", OTA_NODE) + data1
                conn.sendall(packet)
                return

            print(f"size of data1=={length}")
            conn.sendall(packet)

            print(f"Pkt sent no:{ctr+1}")
            ctr += 1
        except Exception as e:
            print(f"Error: {e}")
            return

def ota_node():
    print("OTA Node selected.")
    list_clients()
    with lock:
        if not clients:
            print("No clients connected.")
            return
        try:
            choice = int(input("Select client number to send OTA: ")) - 1
            if choice < 0 or choice >= len(clients):
                print("Invalid choice.")
                return
            client_address = list(clients.keys())[choice]
            firmware_path = input("Enter firmware file path: ")
            try:
                fp = open(firmware_path, "rb")
            except Exception as e:
                print(f"Failed to open firmware file: {e}")
                return
            nodeid = input("Enter Node ID: ")
            if not nodeid.isdigit():
                print("Invalid Node ID. It should be a number.")
                return
            nodeid = int(nodeid)
            conn = clients[client_address]
            process_ota_node(conn, fp, nodeid)
            fp.close()
            print("Sending FW OTA Node completed. Please wait update to the node finished. It may take about 30 minutes.")
        except Exception as e:
            print(f"OTA Node error: {e}")

# Function to display the menu
def menu():
    while True:
        print("\nMenu:")
        print("1. List connected clients")
        print("2. Send data to client")
        print("3. Control Binary Switch")
        print("4. Control Irrigaton System")
        print("5. OTA Bridge")
        print("6. OTA NCP")
        print("7. OTA Node")
        print("x. Exit")
        choice = input("Enter your choice: ")
        if choice == "1":
            list_clients()
        elif choice == "2":
            send_data()
        elif choice == "3":
            send_binary_switch_data()
        elif choice == "4":
            send_irrigation_data()
        elif choice == "5":
            ota_bridge()
        elif choice == "6":
            ota_ncp()
        elif choice == "7":
            ota_node()
        elif choice == "x":
            print("Exiting...")
            break
        else:
            print("Invalid choice. Please try again.")

# Main function to start the TLS server
def start_tls_server(port=8000):
    # Create a TCP socket
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(("0.0.0.0", port))
    server.listen(5)
    print(f"Server listening on port {port}...")

    # Wrap the socket with TLS
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile="server-cert.pem", keyfile="server-key.pem")
    tls_server = context.wrap_socket(server, server_side=True)

    # Start the menu in a separate thread
    threading.Thread(target=menu, daemon=True).start()

    while True:
        client_socket, client_address = tls_server.accept()
        print(f"Secure connection established with {client_address}")
        clients[client_address] = client_socket

if __name__ == "__main__":
    context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
    print([c['name'] for c in context.get_ciphers()])

    # Get port from command line argument or default to 8000
    port = 8000
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except Exception:
            print("Invalid port argument, using default 8000.")
    start_tls_server(port)
