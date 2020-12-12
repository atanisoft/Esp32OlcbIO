# ESP32 OpenLCB IO Board Firmware

## Features

The ESP32 OpenLCB IO Board supports all features of the base IO Board PCB. Some
of the functionality has been customized in this firmware as described below.

### Input/Output pins

The ESP32 OpenLCB IO Board firmware exposes sixteen input/output connections with
two of these being input only. Additionally there are two buttons which can be
used to generate events from the base IO board.

### Factory reset

The Factory Reset button on the base IO Board can be held during startup of the
node to initiate one of the behavors below:

* Hold for five seconds to regenerate event identifiers for all eighteen
input/output pins.
* Hold for ten seconds to erase all OpenLCB (LCC) configuration data and return
the node to it's default configuration. NOTE: This will not clear persistent
WiFi settings.
* Hold both the Factory Reset and User buttons during startup to force the node
into the bootloader download mode. This mode can be used to update the firmware
from within JMRI.
* Press anytime after boot to generate a pair of events.

### User Button

Pressing the User button will generate a pair of events.

## Building

The ESP32 OpenLCB IO Board requires ESP-IDF v4.0 or later. When checking out the
code be sure to checkout recursively as:
`git clone --recursive git@github.com:atanisoft/esp32olcbio.git`.
If you receive an error related to littlefs it is likely that the esp_littlefs
dependencies are not present, to fix this navigate to `components/esp_littlefs`
and execute `git submodule update --init --recursive`.

## Updating the firmware

The easiest way to update the firmware on the ESP32 OpenLCB IO Board is to upload
it via the JMRI Firmware Upload tool via the TWAI (CAN) interface since you do not
need to remove the ESP32 OpenLCB IO Board from the train layout. Alternatively,
the updated firmware can be uploaded via the web interface.
