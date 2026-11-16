# Dasai Smart

**An ESP32-based interactive desktop companion featuring animated facial expressions and touch-based interaction.**

## Overview

Dasai Smart is an embedded systems project built around the ESP32 microcontroller. It combines an OLED display, touch sensing and custom graphical expressions to create an interactive desktop companion.

The device displays different facial expressions and animations on an OLED screen, allowing users to interact with it through a touch sensor. The project focuses on embedded programming, hardware interfacing and real-time visual interaction.

## Features

- **Animated Expressions:** Displays custom facial expressions and animations on the OLED screen.
- **Touch Interaction:** Supports user interaction through a capacitive touch sensor.
- **OLED Display:** Uses an I2C-based OLED display to render graphical expressions.
- **Custom Expression Assets:** Includes a collection of expression assets in the `dasaixpressions` directory.
- **ESP32 Integration:** Uses the ESP32 microcontroller for processing and hardware control.
- **Extensible Design:** Supports customization of expressions, animations and interaction logic.

## Hardware Requirements

| Component | Purpose |
|---|---|
| ESP32 Development Board | Main microcontroller |
| OLED Display (I2C) | Expression rendering |
| Touch Sensor | User interaction |
| Jumper Wires | Hardware connections |
| USB Cable | Programming and power supply |

## Circuit Connections

### OLED Display

| OLED Pin | ESP32 Pin |
|---|---|
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| VCC | 3.3V |
| GND | GND |

### Touch Sensor

| Touch Sensor Pin | ESP32 Pin |
|---|---|
| OUT | GPIO 33 |
| VCC | 3.3V |
| GND | GND |

## Project Structure

```text
dasai/
│
├── Dasai_Smart/
│   ├── Dasai_Smart.ino
│   ├── faces.h
│   └── README.txt
│
├── dasaixpressions/
│   └── Expression GIFs and assets
│
└── README.md
```

## Installation and Setup

### 1. Clone the Repository

```bash
git clone https://github.com/saitejx/dasai.git
```

### 2. Open the Project

Navigate to the `Dasai_Smart` directory and open `Dasai_Smart.ino` using the Arduino IDE.

### 3. Configure the Development Environment

- Install Arduino IDE.
- Install ESP32 board support.
- Select the appropriate ESP32 development board.
- Select the connected COM port.
- Install the libraries required by the source code.

### 4. Configure Wi-Fi

If using Wi-Fi functionality, update the SSID and password placeholders in the source code with your network credentials.

### 5. Upload the Firmware

Connect the ESP32 to your computer through USB, compile the sketch and upload the firmware.

### 6. Hardware Setup

Connect the OLED display and touch sensor according to the connection tables. Power on the ESP32 to start the device.

## Customization

The project can be extended through:

- Adding new facial expressions.
- Modifying graphical assets in `faces.h`.
- Implementing additional touch interactions.
- Customizing animation sequences and transitions.
- Integrating additional sensors and hardware modules.

## Future Enhancements

- Expanded facial expression library.
- Additional interaction modes.
- Sound-based interaction.
- Gesture recognition.
- Custom animation transitions.
- Extended smart desktop companion functionality.

## License
