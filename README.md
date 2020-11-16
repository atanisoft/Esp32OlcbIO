# ESP32 IO Board

The ESP32 IO Board is a basic OpenLCB (LCC) IO Board offering 14 input/output
capable pins and two that are input only. Additionally all IO pins are exposed
via two 13 pin headers which can be used for extending the functionality of the
base board via a daughter board approach.

## Powering the IO Board

The IO Board can be powered via an external power supply (12V) or via the
OpenLCB (LCC) Bus power pin. To power via the external power connection bridge
JP1 on pins 1 and 2. To power via the OpenLCB (LCC) bus bridge JP1 pins 2 and 3.

### OpenLCB (LCC) Power requirements

The IO Board will draw around 100mA from the OpenLCB (LCC) Bus when the PWR_POS
pin provides 15VDC. If the PWR_POS pin provides less the node may draw more
current from the bus.

### External Power requirements

The external power supply should be at least 12VDC 500mA to ensure sufficient
current is available for the node to operate as intended.

### Node Brownout detection

If the ESP32 detects a brownout it can (and should!) produce the well-known
event `01.00.00.00.00.00.FF.F1` if possible.

## Pin Mapping

By default almost all pins are exposed for general IO usage by the node,
however a few pins have specialized usage as part of the screw terminals or
expansion headers.

| GPIO Pin | Usage |
| -------- | ----- |
| 0 | IO4 |
| 1 | UART0 TX (not exposed) |
| 2 | IO5 |
| 3 | UART0 RX (not exposed) |
| 4 | CAN RX |
| 5 | CAN TX |
| 6-11 | NOT AVAILABLE (connected to on chip flash) |
| 12 | IO7 |
| 13 | IO8 |
| 14 | IO16 |
| 15 | IO6 |
| 16 | IO3 |
| 17 | IO2 |
| 18 | IO1 |
| 19 | I2C - SDA |
| 20 | NOT AVAILABLE |
| 21 | I2C - SCL |
| 22 | Node Activity LED |
| 23 | WiFi Active LED |
| 24 | NOT AVAILABLE |
| 25 | IO13 |
| 26 | IO14 |
| 27 | IO15 |
| 28-31 | NOT AVAILABLE |
| 32 | IO11 |
| 33 | IO12 |
| 34 | IO9 -- INPUT ONLY |
| 35 | IO10 -- INPUT ONLY |
| 36 (SVP) | User button -- INPUT ONLY |
| 37 | NOT AVAILABLE |
| 38 | NOT AVAILABLE |
| 39 (SVP) | Factory Reset button -- INPUT ONLY |

### Input only pins

The four pins marked as INPUT ONLY in the table above have a 10k pull-up to 3v3 and a 100nF capacitor for debounce.

### User button default behavior

The default behavior of the User button is to emit the well-known event `01.00.00.00.00.00.FE.00` (Node Identity).

## Extending the base board

A template daughter board will be provided in the near future.