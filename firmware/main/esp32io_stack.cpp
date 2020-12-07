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
 * \file esp32io_stack.cpp
 *
 * OpenMRN Stack components initialization.
 *
 * @author Mike Dunston
 * @date 1 September 2020
 */

#include "sdkconfig.h"
#include "cdi.hxx"
#include "DelayRebootHelper.hxx"
#include "EventBroadcastHelper.hxx"
#include "FactoryResetHelper.hxx"
#include "fs.hxx"
#include "hardware.hxx"
#include "HealthMonitor.hxx"
#include "NodeRebootHelper.hxx"
#include "nvs_config.hxx"
#include "web_server.hxx"

#include <CDIHelper.hxx>
#include <esp_sntp.h>
#include <esp_task.h>
#include <freertos_includes.h>
#include <freertos_drivers/esp32/Esp32WiFiManager.hxx>
#include <freertos_drivers/esp32/Esp32HardwareTwai.hxx>
#include <openlcb/ConfiguredProducer.hxx>
#include <openlcb/MemoryConfigClient.hxx>
#include <openlcb/MultiConfiguredPC.hxx>
#include <openlcb/RefreshLoop.hxx>
#include <openlcb/SimpleStack.hxx>
#include <utils/AutoSyncFileFlow.hxx>
#include <utils/format_utils.hxx>
#include <utils/constants.hxx>
#include <utils/Uninitialized.hxx>

static esp32io::ConfigDef cfg(0);

namespace openlcb
{
    // where the CDI file exists
    const char *const CDI_FILE = "/fs/cdi.xml";

    // This will stop openlcb from exporting the CDI memory space upon start.
    const char CDI_DATA[] = "";

    // Path to where OpenMRN should persist general configuration data.
    const char *const CONFIG_FILENAME = "/fs/config";

    // The size of the memory space to export over the above device.
    const size_t CONFIG_FILE_SIZE = cfg.seg().size() + cfg.seg().offset();

    // Default to store the dynamic SNIP data is stored in the same persistant
    // data file as general configuration data.
    const char *const SNIP_DYNAMIC_FILENAME = "/fs/config";

    /// Defines the identification information for the node. The arguments are:
    ///
    /// - 4 (version info, always 4 by the standard
    /// - Manufacturer name
    /// - Model name
    /// - Hardware version
    /// - Software version
    ///
    /// This data will be used for all purposes of the identification:
    ///
    /// - the generated cdi.xml will include this data
    /// - the Simple Node Ident Info Protocol will return this data
    /// - the ACDI memory space will contain this data.
    const SimpleNodeStaticValues SNIP_STATIC_DATA =
    {
        4,
        SNIP_PROJECT_PAGE,
        SNIP_PROJECT_NAME,
        SNIP_HW_VERSION,
        SNIP_SW_VERSION
    };
}

extern "C" void enter_bootloader()
{
    node_config_t config;
    if (load_config(&config) != ESP_OK)
    {
        default_config(&config);
    }
    config.bootloader_req = true;
    save_config(&config);
    LOG(INFO, "[Bootloader] Rebooting into bootloader");
    reboot();
}

// Configurable IO pins
constexpr const Gpio *const CONFIGURABLE_GPIO_PIN_INSTANCES[] =
{
    IO1_Pin::instance(),  IO2_Pin::instance(),  IO3_Pin::instance()
  , IO4_Pin::instance(),  IO5_Pin::instance(),  IO6_Pin::instance()
  , IO7_Pin::instance(),  IO8_Pin::instance()
  , IO11_Pin::instance(), IO12_Pin::instance(), IO13_Pin::instance()
  , IO14_Pin::instance(), IO15_Pin::instance(), IO16_Pin::instance()
};

static int config_fd;
uninitialized<openlcb::SimpleCanStack> stack;
uninitialized<openlcb::MemoryConfigClient> memory_client;
uninitialized<openlcb::ConfiguredProducer> gpi_9;
uninitialized<openlcb::ConfiguredProducer> gpi_10;
uninitialized<openlcb::ConfiguredProducer> factory_button;
uninitialized<openlcb::ConfiguredProducer> user_button;
uninitialized<openlcb::MultiConfiguredPC> multi_pc;
std::unique_ptr<openlcb::RefreshLoop> refresh_loop;
uninitialized<Esp32WiFiManager> wifi_manager;
uninitialized<AutoSyncFileFlow> config_sync;
uninitialized<esp32io::FactoryResetHelper> factory_reset_helper;
uninitialized<esp32io::EventBroadcastHelper> event_helper;
uninitialized<esp32io::DelayRebootHelper> delayed_reboot;
uninitialized<esp32io::HealthMonitor> health_mon;
uninitialized<esp32io::NodeRebootHelper> node_reboot_helper;
#if CONFIG_SNTP
static bool sntp_configured = false;
#endif // CONFIG_SNTP

#if CONFIG_OLCB_ENABLE_TWAI
Esp32HardwareTwai twai(CONFIG_TWAI_RX_PIN, CONFIG_TWAI_TX_PIN);
#endif // CONFIG_OLCB_ENABLE_TWAI

#if CONFIG_SNTP
static bool sntp_callback_called_previously = false;
static void sntp_received(struct timeval *tv)
{
  // if this is the first time we have been called, check if the modification
  // timestamp on the cdi.xml is older than 2020-01-01 and if so reset the
  // modification/access timestamp to current.
  if (!sntp_callback_called_previously)
  {
    struct stat statbuf;
    stat(openlcb::CDI_FILE, &statbuf);
    time_t mod = statbuf.st_mtime;
    struct tm timeinfo;
    localtime_r(&mod, &timeinfo);
    // check if the timestamp on cdi.xml is prior to 2020 and if so, force the
    // access/modified timestamps to the current time.
    if (timeinfo.tm_year < (2020 - 1900))
    {
      LOG(INFO, "[SNTP] Updating timestamps on configuration files");
      utime(openlcb::CDI_FILE, NULL);
      utime(openlcb::CONFIG_FILENAME, NULL);
    }
    sntp_callback_called_previously = true;
  }
  time_t new_time = tv->tv_sec;
  LOG(INFO, "[SNTP] Received time update, new localtime: %s", ctime(&new_time));
}
#endif // CONFIG_SNTP

void factory_reset_events()
{
    LOG(WARNING, "[CDI] Resetting event IDs");
    stack->factory_reset_all_events(cfg.seg().internal_config()
                                  , CONFIG_OLCB_NODE_ID, config_fd);
    fsync(config_fd);
}

#ifndef CONFIG_WIFI_STATION_SSID
#define CONFIG_WIFI_STATION_SSID ""
#endif

#ifndef CONFIG_WIFI_STATION_PASSWORD
#define CONFIG_WIFI_STATION_PASSWORD ""
#endif

#ifndef CONFIG_WIFI_SOFTAP_SSID
#define CONFIG_WIFI_SOFTAP_SSID "esp32io"
#endif

#ifndef CONFIG_WIFI_SOFTAP_PASSWORD
#define CONFIG_WIFI_SOFTAP_PASSWORD "esp32io"
#endif

#ifndef CONFIG_WIFI_HOSTNAME_PREFIX
#define CONFIG_WIFI_HOSTNAME_PREFIX "esp32io_"
#endif

#ifndef CONFIG_WIFI_SOFTAP_CHANNEL
#define CONFIG_WIFI_SOFTAP_CHANNEL 1
#endif

void start_openlcb_stack(node_config_t *config, bool reset_events
                       , bool brownout_detected)
{
    LOG(INFO, "[SNIP] version:%d, manufacturer:%s, model:%s, hw-v:%s, sw-v:%s"
      , openlcb::SNIP_STATIC_DATA.version
      , openlcb::SNIP_STATIC_DATA.manufacturer_name
      , openlcb::SNIP_STATIC_DATA.model_name
      , openlcb::SNIP_STATIC_DATA.hardware_version
      , openlcb::SNIP_STATIC_DATA.software_version);
    stack.emplace(config->node_id);
    memory_client.emplace(stack->node(), stack->memory_config_handler());
    gpi_9.emplace(stack->node(), cfg.seg().gpi().entry<0>()
                , IO9_Pin::instance());
    gpi_10.emplace(stack->node(), cfg.seg().gpi().entry<1>()
                 , IO10_Pin::instance());
    factory_button.emplace(stack->node(), cfg.seg().gpi().entry<2>()
                         , FACTORY_RESET_Pin::instance());
    user_button.emplace(stack->node(), cfg.seg().gpi().entry<3>()
                      , USER_BUTTON_Pin::instance());
    multi_pc.emplace(stack->node(), CONFIGURABLE_GPIO_PIN_INSTANCES
                   , ARRAYSIZE(CONFIGURABLE_GPIO_PIN_INSTANCES)
                   , cfg.seg().gpio());
    refresh_loop.reset(
        new openlcb::RefreshLoop(stack->node()
                               , { gpi_9->polling(), gpi_10->polling()
                               , factory_button->polling()
                               , user_button->polling()
                               , multi_pc->polling() }));
    factory_reset_helper.emplace(cfg, config->node_id);
    event_helper.emplace(stack.get_mutable());
    delayed_reboot.emplace(stack->service());
    health_mon.emplace(stack->service());

    stack->set_tx_activity_led(LED_ACTIVITY_Pin::instance());

    wifi_manager.emplace(CONFIG_WIFI_STATION_SSID, CONFIG_WIFI_STATION_PASSWORD
                       , stack.get_mutable(), cfg.seg().wifi()
                       , CONFIG_WIFI_HOSTNAME_PREFIX
                       , (wifi_mode_t)CONFIG_WIFI_MODE
                       , nullptr, ip_addr_any, CONFIG_WIFI_SOFTAP_SSID
                       , CONFIG_WIFI_SOFTAP_PASSWORD
                       , WIFI_AUTH_WPA2_PSK, CONFIG_WIFI_SOFTAP_CHANNEL);
    
    wifi_manager->register_network_status_led(LED_WIFI_Pin::instance());

    wifi_manager->register_network_up_callback(
    [](esp_interface_t iface, uint32_t ip)
    {
// TODO: move this inside Esp32WiFiManager
#if CONFIG_SNTP
        if (!sntp_configured)
        {
            LOG(INFO, "[SNTP] Polling %s for time updates",
                CONFIG_SNTP_SERVER);
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, CONFIG_SNTP_SERVER);
            sntp_set_time_sync_notification_cb(sntp_received);
            sntp_init();
            sntp_configured = true;
#ifdef CONFIG_TIMEZONE
            LOG(INFO, "[TimeZone] %s", CONFIG_TIMEZONE);
            setenv("TZ", CONFIG_TIMEZONE, 1);
            tzset();
#endif // CONFIG_TIMEZONE
        }
#endif // CONFIG_SNTP
    });

#if CONFIG_OLCB_PRINT_ALL_PACKETS
    stack->print_all_packets();
#endif

    // Create / update CDI, if the CDI is out of date a factory reset will be
    // forced.
    if (CDIHelper::create_config_descriptor_xml(cfg, openlcb::CDI_FILE
                                              , stack.get_mutable()))
    {
        LOG(WARNING, "[CDI] Forcing factory reset due to CDI update");
        unlink(openlcb::CONFIG_FILENAME);
    }

    // Create config file and initiate factory reset if it doesn't exist or is
    // otherwise corrupted.
    config_fd =
        stack->create_config_file_if_needed(cfg.seg().internal_config()
                                          , CDI_VERSION
                                          , openlcb::CONFIG_FILE_SIZE);

    if (reset_events)
    {
        factory_reset_events();
    }

    // Create auto-sync hook since LittleFS will not persist the config until
    // fflush or file close.
    // NOTE: This can be removed if/when OpenMRN issues an fsync() as part of
    // processing MemoryConfigDefs::COMMAND_UPDATE_COMPLETE.
    config_sync.emplace(stack->service(), config_fd
                      , SEC_TO_USEC(CONFIG_OLCB_CONFIG_SYNC_SEC));

    // Configure the node reboot helper to allow safe shutdown of file handles
    // and file systems etc.
    node_reboot_helper.emplace(stack.get_mutable(), config_fd
                             , config_sync.get_mutable());

    init_webserver(memory_client.get_mutable(), config->node_id);

#if CONFIG_OLCB_ENABLE_TWAI
    stack->executor()->add(new CallbackExecutable([]
    {
        // Initialize the TWAI driver
        twai.hw_init();
        stack->add_can_port_async("/dev/twai/twai0");
    }));
#endif // CONFIG_OLCB_ENABLE_TWAI

    if (brownout_detected)
    {
        // Queue the brownout event to be sent.
        stack->executor()->add(new CallbackExecutable([]()
        {
            LOG_ERROR("[Brownout] Detected a brownout reset, sending event");
            stack->send_event(openlcb::Defs::NODE_POWER_BROWNOUT_EVENT);
        }));
    }

    // Start the stack in the background using it's own task.
    stack->start_executor_thread("OpenMRN"
                               , config_arduino_openmrn_task_priority()
                               , config_arduino_openmrn_stack_size());
}