# ESP32 OpenLCB IO Board (Arduino IDE variant)

The ESP32 OpenLCB IO Board is a basic OpenLCB (LCC) node offering 10 input pins
and eight output pins. This is a very simplified version of the main Esp32OlcbIO
project built upon ESP-IDF with a number of features omitted:

1. Web interface is not available.
2. CAN Bootloader is not supported.
3. Extension PCBs are not supported by this code but could be added by the user.

This code is intended as a starting point for implementing your own arduino-esp32
based OpenLCB (LCC) node using the Esp32OlcbIO PCB.

**NOTE** All IO pins are 3v3 and are not 5v tolerant.

## Output pins

| GPIO Pin | Name | Notes |
| -------- | ---- | ----- |
| 18 | IO1 | |
| 17 | IO2 | |
| 16 | IO3 | |
| 0 | IO4 | often has pull-up resistor |
| 2 | IO5 | often has pull-down resistor |
| 15 | IO6 | often has pull-up resistor |
| 12 | IO7 | often has pull-down resistor |
| 13 | IO8 |

## Input pins

| GPIO Pin | Name | Notes |
| -------- | ---- | ----- |
| 34 | IO9 | PCB has 10k pull-up resistor and 100nF capacitor as debounce |
| 35 | IO10 | PCB has 10k pull-up resistor and 100nF capacitor as debounce |
| 32 | IO11 | |
| 33 | IO12 | |
| 25 | IO13 | |
| 26 | IO14 | |
| 27 | IO15 | |
| 14 | IO16 | |
| 36 (SVP) | User button | PCB has 10k pull-up resistor and 100nF capacitor as debounce |
| 39 (SVP) | Factory Reset button | PCB has 10k pull-up resistor and 100nF capacitor as debounce |