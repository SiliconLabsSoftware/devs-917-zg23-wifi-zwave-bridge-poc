# Orbit Wi-Fi/Z-Wave Bridge Gateway

![Project Type](https://img.shields.io/badge/Type-Firmware-green)
[![Bluetooth Low Energy](https://img.shields.io/badge/BLE-Bluetooth%20Low%20Energy-blue)](https://www.bluetooth.com/learn-about-bluetooth/tech-overview/)
[![WiFi](https://img.shields.io/badge/WiFi-IEEE%20802.11ax-yellow)](https://vi.wikipedia.org/wiki/Wi-Fi_6)
![Simplicity SDK Version](https://img.shields.io/badge/SimplicitySDK-v2024.12.0-green)
![WiseConnect SDK Version](https://img.shields.io/badge/WiseConnectSDK-v3.4.0-green)

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Prerequisites/Setup Requirements](#prerequisitessetup-requirements)
  - [Hardware Requirements](#hardware-requirements)
  - [Software Requirements](#software-requirements)
  - [Setup Diagram](#setup-diagram)
- [Build Guide](#build-guide)

## Overview

- This project demonstrates a Wi-Fi/Z-Wave Long Range (LR) bridge gateway proof of concept (PoC). The gateway will act as a bridge between Z-Wave devices and Orbit servers, enabling communication and control via a TLS socket connection.

## Key Features

Z-Wave
1. Controller Functions: Control up to 32 Z-Wave end devices, including both mesh and Z-Wave Long Range (LR) nodes.
2. Irrigation Support: Initial support for irrigation systems using the Z-Wave Irrigation Command Class, including ON/OFF commands and simple serial data packet transmission.
3. Expandability: Ability to integrate third-party Z-Wave end devices and associated command classes in the future.
4. Network Commissioning: Support for Z-Wave S2 and QR code-based end node network commissioning.
5. OTA Updates: Support for Over-the-Air (OTA) updates for Z-Wave end nodes, with update images obtained via BLE or Wi-Fi.

Wi-Fi
1. TLS Socket Interface: Wi-Fi communication via a TLS socket, supporting addresses and packets for all Z-Wave end nodes.
2. Address Translation: IP address to Z-Wave address translation for Z-Wave end devices.
3. Wi-Fi 6 Support: Support for 2.4 GHz Wi-Fi 6, similar to the SiWx917 platform.

BLE (Bluetooth Low Energy)
1. Wi-Fi Commissioning: BLE support for Wi-Fi commissioning to a local Access Point (AP).
2. GATT Connection: Support for standard BLE GATT connections and advertising.
3. Legacy Product Control: Support for BLE 125kbps Long Range (LR) for controlling Orbit legacy products.

## Prerequisites/Setup Requirements

### Hardware Requirements

- Windows PC
- Silicon Labs Si917 Evaluation Kit [WPK(BRD4002) + BRD4342A]
- xG23-RB4210A Radio Board

### Software Requirements

- Simplicity Studio

### SDK Version

- Simplicity SDK v2024.12.0
- WiseConnect 3 v3.4.0

### Setup Diagram

//TODO

## Build Guide

1. Open Simplicity Studio 5.
2. Select File/Import.
<p align="center">
  <img src=resources/readme/build-guide-1.png>
</p>
3. In Import tab, select "More Import Options".
<p align="center">
  <img src=resources/readme/build-guide-2.png>
</p>
4. Select "Existing Projects into Workplaces" then Next.
<p align="center">
  <img src=resources/readme/build-guide-3.png>
</p>
5. Browse the cloned project directory
<p align="center">
  <img src=resources/readme/build-guide-4.png>
</p>
6. Right click on the project then select "Build Project".
<p align="center">
  <img src=resources/readme/build-guide-5.png>
</p>
7. After the build process has completed, in folder "Binaries", right click "DevS_Orbit_PoC.s37" then select "Flash to Device".
<p align="center">
  <img src=resources/readme/build-guide-6.png>
</p>
