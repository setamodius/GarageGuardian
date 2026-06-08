# Garage Guardian

Garage Guardian is an ESP32-based BLE access control system designed to simplify garage and door access while improving security.

The system connects to one or more iTAG BLE remotes and listens for button notifications. When an authorized device triggers an event, the ESP32 activates an electric strike for a configurable period of time, allowing the door to be opened.

The project was created with a simple goal:

> Build a reliable, low-cost, offline access control system using readily available components.

## Features

- ESP32 based
- BLE iTAG support
- Multiple authorized devices
- Electric strike control
- Automatic BLE reconnection
- Audible status notifications
- Visual status indication
- Offline operation (no Wi-Fi required)
- Open source

## Hardware

- ESP32
- BLE iTAG
- Electric strike
- N-Channel MOSFET
- LM2596 DC-DC converter
- Status LED
- Buzzer
- 12V power supply

## Diagram
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
      │ GPIO26 ├───────────────┐            │
      │ GPIO25 ├──[220Ω]──|>|──┴── GND      │
      │ GPIO33 ├────────────── Buzzer       │
      └───┬────┘                            │
          │                                 │
          │ GND                             │
          ▼                                 │
         GND                                │
                                           │
                                        Drain
                                      ┌────────┐
                                      │        │
                         GPIO26 ─────│ Gate   │
                                      │ MOSFET │
                                      │ Source │
                                      └───┬────┘
                                          │
                                          ▼
                                         GND
```

### Flyback Diode

```text
              +12V
                │
                ├───────────────┐
                │               │
                │          Cathode
                │             │
                │          ┌──┴──┐
                │          │Diode│
                │          └──┬──┘
                │             │
                │           Anode
                │             │
        ┌───────┴───────┐     │
        │ Electric      │     │
        │ Strike Lock   │     │
        └───────┬───────┘     │
                │             │
                └─────────────┘
                │
              Drain
             MOSFET
```

> All grounds must be connected together: ESP32 GND, LM2596 GND and 12V power supply GND.

## Project Status

Working prototype completed.
