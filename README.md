# Garage Guardian

Garage Guardian is an ESP32-based BLE access control system designed to simplify garage and door access while improving security.

The system connects to one or more iTAG BLE remotes and listens for button notifications. When an authorized device triggers an event, the ESP32 activates an electric strike for a configurable period of time, allowing the door to be opened.

The project was created with a simple goal:

> Build a reliable, low-cost, offline access control system using readily available components.

## Features

- ESP32 based
- BLE iTAG support (up to 5 beacons)
- RSSI-based proximity auto-open
- Button press detection via BLE notifications
- Electric strike control
- Automatic BLE reconnection
- Watchdog timer (60s) for stability
- Audible status notifications (passive buzzer)
- Visual status indication (LED)
- WiFi AP mode for web-based beacon management
- NVS persistent beacon storage
- Serial command interface

## Web Interface

The ESP32 starts a WiFi access point on boot:

- **SSID:** `GarageDoor`
- **Password:** `garage123`
- **URL:** `http://192.168.4.1`

The web interface allows you to:

- View beacon and BLE connection status
- Add or remove authorized beacons
- Scan nearby BLE devices and add them directly

## Serial Commands

Connect via serial at 115200 baud:

| Command | Description |
|---------|-------------|
| `LIST` | List all stored beacons |
| `ADD xx:xx:xx:xx:xx:xx` | Add a beacon by MAC address |
| `REMOVE xx:xx:xx:xx:xx:xx` | Remove a beacon by MAC address |

## Hardware

- ESP32
- BLE iTAG
- Electric strike
- N-Channel MOSFET
- LM2596 DC-DC converter (12V → 5V)
- Status LED
- Passive buzzer
- 12V power supply

## Wiring

```text
                    +12V Power Supply
                          │
          ┌───────────────┴────────────────┐
          │                                │
          ▼                                ▼
   ┌─────────────┐                 ┌─────────────────┐
   │   LM2596    │                 │ Electric Strike │
   │  12V → 5V   │                 │      Lock       │
   └──────┬──────┘                 └────────┬────────┘
          │                                 │
          │ 5V                              │
          ▼                                 │
      ┌────────┐                            │
      │ ESP32  │                            │
      │        │                            │
      │ GPIO26 ├───────────────────────── Gate (MOSFET)
      │ GPIO25 ├──[220Ω]──|>|──── GND      │
      │ GPIO33 ├─────────────────── Buzzer │
      └───┬────┘                            │
          │                                 │
          │ GND                             │
          ▼                               Drain
         GND                            ┌────────┐
                                        │ MOSFET │
                              Strike ───┤ Drain  │
                              GPIO26 ───┤ Gate   │
                                 GND ───┤ Source │
                                        └────────┘
```

### GPIO Pinout

| Pin | Function |
|-----|----------|
| GPIO26 | MOSFET Gate (relay) |
| GPIO25 | Status LED (via 220Ω) |
| GPIO33 | Passive buzzer |

### Flyback Diode

Place a flyback diode across the electric strike coil to suppress voltage spikes:

```text
              +12V
                │
                ├───────────────┐
                │               │
                │          Cathode (Diode)
                │               │
                │           Anode
                │               │
        ┌───────┴───────┐       │
        │ Electric      │       │
        │ Strike Lock   │       │
        └───────┬───────┘       │
                └───────────────┘
                │
              Drain (MOSFET)
```

> All grounds must be connected together: ESP32 GND, LM2596 GND, and 12V power supply GND.

## Project Status

Working prototype completed.
