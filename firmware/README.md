# ESP32 IO Board Firmware

## Building

The ESP32 IO Board requires ESP-IDF v4.0 or later. When checking out the code
be sure to checkout recursively as: `git clone --recursive git@github.com:atanisoft/esp32io.git`.
If you receive an error related to littlefs it is likely that the esp_littlefs
dependencies are not present, to fix this navigate to `components/esp_littlefs`
and execute `git submodule update --init --recursive`.
