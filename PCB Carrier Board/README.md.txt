# HerComfort PCB Carrier Board

Custom raw carrier PCB developed for the HerComfort wearable menstrual comfort belt.

## PCB Overview

The carrier board integrates and connects the main HerComfort hardware modules:

- Seeed Studio XIAO ESP32C6
- MPU6050 IMU
- Muscle BioAmp Candy
- DS18B20 temperature sensor
- TP4056 Li-ion charging module
- Mini-360 buck converter
- AO3400A MOSFET heater control
- AO3400A MOSFET vibration motor control
- 1N5819 flyback diode
- Mode push button
- Li-ion battery
- Heating pad
- Vibration motor

## Schematic

![HerComfort PCB Schematic](Images/schematic.png)

## PCB Front View

![PCB Front](Images/pcb_front.png)

## PCB Back View

![PCB Back](Images/pcb_back.png)

## Front Routing

![Front Routing](Images/routing_front.png)

## Back Routing

![Back Routing](Images/routing_back.png)

## Main GPIO Mapping

| Function | XIAO Pin | GPIO |
|---|---|---|
| Mode Button | D0 | GPIO0 |
| EMG | D1 | GPIO1 |
| I2C SDA | D4 | GPIO22 |
| I2C SCL | D5 | GPIO23 |
| Motor | D7 | GPIO17 |
| Temperature | D8 | GPIO19 |
| Heater | D10 | GPIO18 |

## Power Architecture

```text
Li-ion Battery
      |
    TP4056
      |
 Protected Battery Output
      |
      +---- XIAO ESP32C6
      |
      +---- Mini-360
               |
              3.3V
               |
        Heater + Motor