/** \copyright
 * Copyright (c) 2020, Mike Dunston
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file esp32io_bootloader.cpp
 *
 * Firmware download bootloader support for the ESP32 IO Board.
 *
 * @author Mike Dunston
 * @date 30 November 2020
 */

#include "sdkconfig.h"
#include "hardware.hxx"

#include <bootloader_hal.h>
#include "Esp32BootloaderHal.hxx"

extern "C"
{

void bootloader_hw_set_to_safe(void)
{
    LOG(VERBOSE, "[Bootloader] bootloader_hw_set_to_safe");
    LED_WIFI_Pin::hw_init();
    LED_ACTIVITY_Pin::hw_init();
}

bool request_bootloader(void)
{
    LOG(VERBOSE, "[Bootloader] request_bootloader");
    // Default to allow bootloader to run since we are not entering the
    // bootloader loop unless requested by app_main.
    return true;
}

void bootloader_led(enum BootloaderLed led, bool value)
{
    LOG(VERBOSE, "[Bootloader] bootloader_led(%d, %d)", led, value);
    if (led == LED_ACTIVE)
    {
        LED_ACTIVITY_Pin::instance()->write(value);
    }
    else if (led == LED_WRITING)
    {
        LED_WIFI_Pin::instance()->write(value);
    }
    else if (led == LED_REQUEST)
    {
        LOG(INFO, "[Bootloader] Preparing to receive firmware");
        LOG(INFO, "[Bootloader] Current partition: %s", current->label);
        LOG(INFO, "[Bootloader] Target partition: %s", target->label);
    }
}

} // extern "C"

void start_bootloader_stack(uint64_t id)
{
    esp32_bootloader_run(id, CONFIG_TWAI_TX_PIN, CONFIG_TWAI_RX_PIN, true);
}