# Automated-Grain-Crop-Drying-Controller
ESP32 Automated Grain & Crop Drying Controller

An advanced embedded drying-control system built on the ESP32 (ESP-IDF/FreeRTOS architecture) to automate crop moisture regulation and prevent post-harvest spoilage. The system continuously monitors ambient temperature and humidity alongside grain moisture content to dynamically regulate ventilation and heating element relays.

---

## Live Simulation
Run the live circuit simulation on Wokwi:  
[Click Here to Launch Wokwi Simulation](YOUR_WOKWI_SHARE_LINK_HERE)

---

## Key Features
* Dual-Sensor Monitoring: Continuously tracks ambient conditions (DHT22) and grain moisture levels via simulated analog input.
* Smart Actuation & Vent Control: Drives a relay-controlled fan/heater and dynamically adjusts air vent angles using PWM servo motor control.
* Safety Throttling: Automatically disengages heating relays if ambient temperatures breach threshold safety limits.
* User Interface: Features an I2C 16x2 LCD screen for real-time status output and a 4x4 matrix keypad for target moisture grade selection.

---

## Circuit & Components
* MCU: ESP32 DevKit C V4
* Sensors: DHT22 (Temp & Humidity), Slide Potentiometer (Grain Moisture Simulation)
* Actuators: 5V Relay Module (Fan/Heater), PWM Servo Motor (Air Vent)
* Display & Control: 16x2 I2C LCD, 4x4 Membrane Keypad, Status LEDs

---

## Repository Structure
```text
.
├── README.md
└── grain-drying-wokwi/
    ├── sketch.ino       # ESP32 C / FreeRTOS firmware logic
    └── diagram.json     # Wokwi schematic wiring layout
