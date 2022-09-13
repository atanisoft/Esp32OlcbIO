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
 * \file esp32io.cpp
 *
 * Program entry point for the ESP32 IO Board.
 *
 * @author Mike Dunston
 * @date 4 July 2020
 */
#include "sdkconfig.h"
#include "fs.hxx"
#include "hardware.hxx"
#include "NodeRebootHelper.hxx"
#include "nvs_config.hxx"

#include <algorithm>
#include <driver/i2c.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp32/rom/rtc.h>
#include <freertos_includes.h>
#include <freertos_drivers/esp32/Esp32SocInfo.hxx>
#include <openlcb/SimpleStack.hxx>

///////////////////////////////////////////////////////////////////////////////
// Enable usage of select() for GridConnect connections.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST_TRUE(gridconnect_tcp_use_select);

#if CONFIG_OLCB_GC_NEWLINES
///////////////////////////////////////////////////////////////////////////////
// This will generate newlines after GridConnect each packet being sent.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST_TRUE(gc_generate_newlines);
#endif // CONFIG_OLCB_GC_NEWLINES

///////////////////////////////////////////////////////////////////////////////
// Increase the GridConnect buffer size to improve performance by bundling more
// than one GridConnect packet into the same send() call to the socket.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(gridconnect_buffer_size, CONFIG_OLCB_GC_BUFFER_SIZE);

///////////////////////////////////////////////////////////////////////////////
// Increase the time for the buffer to fill up before sending it out over the
// socket connection.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(gridconnect_buffer_delay_usec, CONFIG_OLCB_GC_BUFFER_DELAY_USEC);

///////////////////////////////////////////////////////////////////////////////
// This limits the number of outbound GridConnect packets which limits the
// memory used by the BufferPort.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(gridconnect_bridge_max_outgoing_packets,
               CONFIG_OLCB_GC_OUTBOUND_PACKET_LIMIT);

///////////////////////////////////////////////////////////////////////////////
// Increase the listener backlog to improve concurrency.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(socket_listener_backlog, 2);

///////////////////////////////////////////////////////////////////////////////
// Increase the CAN RX frame buffer size to reduce overruns when there is high
// traffic load (ie: large datagram transport).
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(can_rx_buffer_size, CONFIG_OLCB_TWAI_RX_BUFFER_SIZE);
OVERRIDE_CONST(can_tx_buffer_size, CONFIG_OLCB_TWAI_TX_BUFFER_SIZE);

/// Number of seconds to hold the Factory Reset button to force clear all
/// stored configuration data.
static constexpr int8_t FACTORY_RESET_HOLD_TIME = 10;

/// Number of seconds to hold the Factory Reset button to force regeneration of
/// all Event IDs. NOTE: This will *NOT* clear WiFi configuration data.
static constexpr int8_t FACTORY_RESET_EVENTS_HOLD_TIME = 5;

namespace esp32io
{

void start_openlcb_stack(node_config_t *config, bool reset_events
                       , bool brownout_detected, bool wifi_verbose);

} // namespace esp32io

void start_bootloader_stack(uint64_t node_id);

extern "C"
{

void *node_reboot(void *arg)
{
    Singleton<esp32io::NodeRebootHelper>::instance()->reboot();
    return nullptr;
}

void reboot()
{
    os_thread_create(nullptr, nullptr, uxTaskPriorityGet(NULL) + 1, 2048
                   , node_reboot, nullptr);
}

ssize_t os_get_free_heap()
{
    return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

static const char * const reset_reasons[] =
{
    "unknown",                  // NO_MEAN                  0
    "power on reset",           // POWERON_RESET            1
    "unknown",                  // no key                   2
    "software reset",           // SW_RESET                 3
    "watchdog reset (legacy)",  // OWDT_RESET               4
    "deep sleep reset",         // DEEPSLEEP_RESET          5
    "reset (SLC)",              // SDIO_RESET               6
    "watchdog reset (group0)",  // TG0WDT_SYS_RESET         7
    "watchdog reset (group1)",  // TG1WDT_SYS_RESET         8
    "RTC system reset",         // RTCWDT_SYS_RESET         9
    "Intrusion test reset",     // INTRUSION_RESET          10
    "WDT Timer group reset",    // TGWDT_CPU_RESET          11
    "software reset (CPU)",     // SW_CPU_RESET             12
    "RTC WDT reset",            // RTCWDT_CPU_RESET         13
    "software reset (CPU)",     // EXT_CPU_RESET            14
    "Brownout reset",           // RTCWDT_BROWN_OUT_RESET   15
    "RTC Reset (Normal)",       // RTCWDT_RTC_RESET         16
};

void app_main()
{
    // capture the reason for the CPU reset
    uint8_t reset_reason = Esp32SocInfo::print_soc_info();
    const esp_app_desc_t *app_data = esp_ota_get_app_description();
    LOG(INFO, "%s uses the OpenMRN library\n"
              "Copyright (c) 2019-2022, OpenMRN\n"
              "All rights reserved.", app_data->project_name);

    // silence all but error messages by default
    esp_log_level_set("*", ESP_LOG_ERROR);

    GpioInit::hw_init();

    nvs_init();

    // load non-CDI based config from NVS.
    bool cleanup_config_tree = false;
    node_config_t config;
    if (load_config(&config) != ESP_OK)
    {
        default_config(&config);
        cleanup_config_tree = true;
    }
    bool reset_events = false;
    bool run_bootloader = false;
    bool wifi_verbose = false;

    // Check for factory reset button being held to GND and the USER button
    // not being held to GND. If this is detected the factory reset process
    // will be started.
    if (FACTORY_RESET_Pin::instance()->is_clr() && 
        USER_BUTTON_Pin::instance()->is_set())
    {
        LED_WIFI_Pin::instance()->set();
        LED_ACTIVITY_Pin::instance()->clr();
        // Count down from the overall factory reset time.
        int8_t hold_time = FACTORY_RESET_HOLD_TIME;
        for (; hold_time > 0 && FACTORY_RESET_Pin::instance()->is_clr();
             hold_time--)
        {
            if (hold_time > FACTORY_RESET_EVENTS_HOLD_TIME)
            {
                LOG(WARNING
                  , "Event ID reset in %d seconds, factory reset in %d seconds."
                  , hold_time - FACTORY_RESET_EVENTS_HOLD_TIME, hold_time);
                LED_ACTIVITY_Pin::toggle();
            }
            else
            {
                LOG(WARNING, "Factory reset in %d seconds.", hold_time);
                LED_ACTIVITY_Pin::instance()->clr();
            }
            usleep(SEC_TO_USEC(1));
            LED_WIFI_Pin::toggle();
        }
        if (FACTORY_RESET_Pin::instance()->is_clr() && hold_time <= 0)
        {
            // if the button is still being held and the hold time expired
            // start a full factory reset.
            LOG(WARNING, "Factory reset triggered!");
            default_config(&config);
            config.force_reset = true;
        }
        else if (hold_time <= FACTORY_RESET_EVENTS_HOLD_TIME)
        {
            // if the button is not being held and the hold time is less than
            // the event id reset count down trigger a reset of events.
            LOG(WARNING, "Reset of events triggered!");
            reset_events = true;
        }
        else
        {
            // The button was released prior to the event id reset limit, do
            // nothing.
            LOG(WARNING, "Factory reset aborted!");
        }
        LED_WIFI_Pin::instance()->clr();
        LED_ACTIVITY_Pin::instance()->clr();
    }
    else if (FACTORY_RESET_Pin::instance()->is_clr() && 
             USER_BUTTON_Pin::instance()->is_clr())
    {
        // If both the factory reset and user button are held to GND it is a
        // request to enter the bootloader mode.
        run_bootloader = true;

        // give a visual indicator that the bootloader request has been ACK'd
        // turn on both WiFi and Activity LEDs, wait ~1sec, turn off WiFi LED,
        // wait ~1sec, turn off Activity LED.
        LED_WIFI_Pin::instance()->set();
        LED_ACTIVITY_Pin::instance()->set();
        vTaskDelay(pdMS_TO_TICKS(1000));
        LED_WIFI_Pin::instance()->clr();
        vTaskDelay(pdMS_TO_TICKS(1000));
        LED_ACTIVITY_Pin::instance()->clr();
    }
    else if (USER_BUTTON_Pin::instance()->is_clr())
    {
        wifi_verbose = true;
        // blink to ack
        LED_WIFI_Pin::instance()->set();
        vTaskDelay(pdMS_TO_TICKS(500));
        LED_ACTIVITY_Pin::instance()->set();
        vTaskDelay(pdMS_TO_TICKS(500));
        LED_WIFI_Pin::instance()->clr();
        LED_ACTIVITY_Pin::instance()->clr();
    }

    // Ensure the LEDs are both OFF when we startup.
    LED_WIFI_Pin::instance()->clr();
    LED_ACTIVITY_Pin::instance()->clr();

    // Check for and reset factory reset flag.
    if (config.force_reset)
    {
        cleanup_config_tree = true;
        config.force_reset = false;
        save_config(&config);
        unlink(openlcb::CONFIG_FILENAME);
    }

    if (config.bootloader_req)
    {
        run_bootloader = true;
        // reset the flag so we start in normal operating mode next time.
        config.bootloader_req = false;
        save_config(&config);
    }

    dump_config(&config);

    if (run_bootloader)
    {
        start_bootloader_stack(config.node_id);
    }
    else
    {
        mount_fs(cleanup_config_tree);
        esp32io::start_openlcb_stack(&config, reset_events,
                                     reset_reason == RTCWDT_BROWN_OUT_RESET,
                                     wifi_verbose);
    }

    // At this point the OpenMRN stack is running in it's own task and we can
    // safely exit from this one. We do not need to cleanup as that will be
    // handled automatically by ESP-IDF.
}

} // extern "C"