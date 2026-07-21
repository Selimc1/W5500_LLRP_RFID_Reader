# W5500 LLRP RFID Reader (Advanced Bare-Metal Implementation)

![STM32](https://img.shields.io/badge/Microcontroller-STM32F407VG-blue)
![Architecture](https://img.shields.io/badge/Architecture-Bare%20Metal-orange)
![Protocol](https://img.shields.io/badge/Protocol-LLRP-red)
![Networking](https://img.shields.io/badge/Networking-W5500-green)

## 📌 Project Overview
This project is an industrial-grade networking application that implements the **Low Level Reader Protocol (LLRP)** over a TCP/IP stack using an **STM32F4** and a **W5500 Ethernet Module**. Unlike standard implementations that rely heavily on operating systems, FreeRTOS, or heavy networking stacks like LwIP, this project is built entirely **bare-metal**. 

It allows the STM32 to act as an LLRP Client, connecting directly to enterprise UHF RFID readers (such as Impinj Speedway or Zebra FX series) over a Local Area Network (LAN), instructing them to read RFID tags, and parsing the complex binary responses in real-time.

## 🏗️ Software Architecture

The project is structured into several highly optimized, register-level layers:

1. **SPI Physical Layer (PHY):** 
   - A bare-metal SPI1 driver (without HAL) that handles high-speed, blocking and non-blocking data transfers between the STM32 and the W5500 chip.
2. **W5500 Core Driver:** 
   - Directly manipulates the W5500's common registers (GAR, SUBR, SHAR, SIPR) for network configuration (MAC, Gateway, Subnet, IP).
   - Manages socket memory allocation, RX/TX buffers, and hardware TCP state machines (Sn_SR).
3. **LLRP Protocol Engine:** 
   - **Message Construction:** Dynamically builds LLRP binary headers and payload structures.
   - **TLV Parser:** A custom-built recursive parser capable of navigating Type-Length-Value (TLV) and Type-Value (TV) parameters within LLRP messages. This is crucial for extracting variable-length EPC (Electronic Product Code) data from `RO_ACCESS_REPORT` packets.
4. **Application Logic:** 
   - Manages the state machine (Disconnected -> Connecting -> Handshake -> Inventory -> Parsing).
   - Streams the extracted hex EPC data out via the STM32's UART for monitoring on a PC terminal.

## 🚀 The LLRP Lifecycle & Supported Messages

To successfully read tags, the STM32 implements a strict sequence of LLRP messages. This driver natively handles the following lifecycle:

1. **`READER_EVENT_NOTIFICATION`:** The RFID reader announces itself upon a successful TCP connection on port `5084`.
2. **`SET_READER_CONFIG`:** The STM32 sends a configuration command to ensure the reader's antennas are powered and KeepAlive signals are configured.
3. **`ADD_ROSPEC` (Reader Operation Spec):** The STM32 defines *how* and *when* the reader should scan for tags (e.g., continuous scanning on Antenna 1).
4. **`ENABLE_ROSPEC` & `START_ROSPEC`:** Commands sent to physically trigger the UHF radio waves and start the inventory process.
5. **`RO_ACCESS_REPORT`:** The reader streams these massive packets back. The STM32's parser digs through the packet to find `EPC-96` or `EPC-Data` parameters, grabbing the exact hexadecimal tag ID.
6. **`KEEPALIVE`:** The STM32 actively responds to `KEEPALIVE` messages with `KEEPALIVE_ACK` to prevent the reader from closing the TCP socket.

## 🛠️ Hardware Requirements & Wiring

- **STM32F407G-DISC1** Development Board.
- **W5500 Ethernet Module** (Connected via SPI1).
- **Industrial LLRP UHF RFID Reader** (Impinj, Zebra, Alien).
- **USB-to-TTL Converter** (For debugging/output).

**Typical SPI Wiring for W5500:**
- `MOSI` -> `PA7`
- `MISO` -> `PA6`
- `SCK`  -> `PA5`
- `CS`   -> `PA4` (Software or Hardware managed)
- `RST`  -> Any GPIO (optional but recommended for hard resets)

## ⚙️ Fault Tolerance & Reliability
In industrial environments, network drops are common. The main loop continuously checks the W5500's Socket Status Register (`Sn_SR`). If the connection drops (e.g., it moves from `SOCK_ESTABLISHED` to `SOCK_CLOSED`), the STM32 automatically clears the socket, re-initializes the TCP client, and re-executes the entire LLRP handshake without requiring a physical reset of the microcontroller.

## 💻 Getting Started
1. Open the project in **STM32CubeIDE**.
2. Navigate to `main.c` (or the network config header) and modify the **W5500 IP settings** to match your local network's subnet (e.g., `192.168.1.50`).
3. Set the **Target IP Address** in the code to the IP of your enterprise RFID reader.
4. Compile using the standard `Make` process in Eclipse and flash via ST-LINK.
5. Connect your TTL serial converter to the STM32's configured UART TX pin at `115200` baud.
6. Once powered, watch the terminal as the STM32 connects, negotiates the LLRP rules, and streams captured RFID EPCs directly to your screen!
