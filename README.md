# HerComfort

HerComfort is a wearable health companion designed for menstrual comfort, physiological monitoring, local therapy control, and connected health support.

## Project Status

HerComfort is currently under active development.

### Completed
- Refined wearable industrial design
- Curved front therapy/control enclosure
- Adjustable elastic belt architecture
- Integrated dry EMG contact placement
- SolidWorks native model
- STEP export
- Mechanical design verification

### In Progress
- Custom HerComfort main PCB
- Bare ESP32-C6 integration
- Integrated Muscle BioAmp Candy analog front-end
- Heating pad control
- Dual vibration motor control
- IMU and temperature sensing
- Battery charging and power management
- BLE firmware and mobile integration

## Repository Structure

### Mechanical
Contains the HerComfort V2 SolidWorks model, STEP geometry, appearances and design views.

### Electronics
Will contain the custom KiCad schematic, PCB layout, BOM and manufacturing files.

### Documentation
Contains project architecture, specifications and technical documentation.

## Mechanical Design

Current therapy module envelope:

- Length: 185 mm
- Height: 78 mm
- Maximum curved width: approximately 80.1 mm
- Maximum depth envelope: approximately 21.4 mm
- Strap width: 48 mm
- USB-C opening: 10 × 3.8 mm
- Three 18 mm dry EMG contact locations on the inner belt

The current CAD represents the external industrial design. Internal electronics packaging and PCB integration are under development.

## Electronics

The final product is intended to use one custom main PCB containing/controling:

- Bare ESP32-C6
- Integrated Muscle BioAmp Candy EMG circuitry
- 6-axis IMU
- Temperature sensing
- Heating pad control
- Two vibration motor channels
- Physical controls
- USB-C charging
- Battery monitoring and protection
- BLE connectivity

KiCad design files will be added in the next project revision.

## Tools

- SolidWorks 2024
- KiCad
- ESP32-C6
- Embedded C/C++
- BLE

## Development

This repository is actively being updated as HerComfort progresses from mechanical design to a complete wearable electronics prototype.
