# sProto
This repo includes files that support the use of the sProto PCB, which extends the ESP32-S3 DevKit board.
The objective is to provide tools and examples for prototyping synthesizers and related music technologies on the ESP32 platform using the Arduino IDE.
The sProto code examples often utilise the [M16](https://github.com/algomusic/M16) sound synthesis library and the [MultiControl](https://github.com/algomusic/MultiControl) UI library.

These GPIO pins are assigned to hardware on the PCB:
- Touch pads 1-4: GPIO 1-4
- Dials A-C: GPIO 5-7
- Button: GPIO 8
- Audio DAC: BCK 38, WS 39, DOUT 40. DIN 41
- MIDI: Input 42, Output 47
- SD Card: CS 11, MOSI 12, CLK 13, MISO 14
- LED: GPIO 48
- I2C (not connected): SDA 18, SCL 21

All other GPIO pins are unassigned.
