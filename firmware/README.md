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

The ESP32 OpenLCB IO Board depends on ESP-IDF v5.1.x and will not build with
earlier versions.

### Configuring ESP-IDF build environment

For Windows environments please use the [Windows Installer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/windows-setup.html)
and select `release/v5.1` when prompted for a version. It is only necessary to
install the basic command line tools, Eclipse (or other IDEs) are optional.

For Linux / Mac environments it is recommended to use `git clone` to setup and
maintain the ESP-IDF build enviornment:
```
git clone https://github.com/espressif/esp-idf.git --depth 1 --branch release/v5.1 esp-idf-v5.1 --recursive
cd esp-idf-v5.1
sh install.sh
```

To use the ESP-IDF build enviornment start a commandline shell and source / run the
`export.sh`, `export.bat` or `export.ps1` script based on your shell.

### Configuring and Building Esp32OlcbIO

Enter the ESP-IDF build environment and navigate to the Esp32OlcbIO source
directory. If this is the first time building the source code it is a good idea
to run `idf.py menuconfig` and configure the default configuration settings for
the Esp32OlcbIO firmware. In general it is not necessary to modify any
configuration settings outside of `OpenLCB Configuration` and `WiFi Configuration`.

Once the configuration has been completed run `idf.py build` to compile the
firmware. If there are no errors you can proceed to programming the firmware.

### Programming the firmware

There are a couple ways to flash the firwmare to the ESP32:

* `idf.py flash` - This leverages the ESP-IDF build environment and should
automatically detect the appropriate communications port for the ESP32.
* [Browser based flash utility](https://espressif.github.io/esptool-js/)

The [Browser based flash utility](https://espressif.github.io/esptool-js/)
only supports using Chrome based browsers, attempting to use Safari will
result in an error dialog being displayed. If you are using this approach,
plug in the ESP32 to an available USB port and click the `Connect` button
which will prompt for the communication port (COM?? on Windows, /dev/ttyACM?
on Linux). Once connected the console output should display information about
the connected ESP32 module.

Enter the following `Flash Address` and `File` using the `Add File` button
as needed:

* 0x1000 - build/bootloader/bootloader.bin
* 0x8000 - build/partition_table/partition-table.bin
* 0xe000 - build/ota_data_initial.bin
* 0x10000 - build/ESP32OlcbIO.bin

Click on `Program` and the binaries will be sent to the ESP32 and it should
reboot automatically.

### Monitoring the firmware startup

After programming a new firmware it is generally a good idea to monitor the
startup of the Esp32OlcbIO firmware to confirm that there are no issues
preventing successful startup.

The easiest way to do this is with `idf.py monitor`.

NOTE: If the ESP32 is *NOT* plugged into the IO PCB it will be necessary to
add a jumper wire between the 3v3 pin and GPIO 39 (may be labeled as SVN or
VN) to prevent Factory Reset from being initiated on startup.

## Updating the firmware

The easiest way to update the firmware on the ESP32 OpenLCB IO Board is to upload
it via the JMRI Firmware Upload tool via the TWAI (CAN) interface since you do not
need to remove the ESP32 OpenLCB IO Board from the train layout. Alternatively,
the updated firmware can be uploaded via the web interface.

If the ESP32 is not connected to the PCB it will be necessary to add a jumper
wire between 3v3 and both the Factory Reset button pin (default 39/SVN) and the
User button pin (default 36/SVP) to prevent the ESP32 from entering bootloader
mode.
