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
#include <freertos_drivers/esp32/Esp32Twai.hxx>
#include <openlcb/ConfiguredProducer.hxx>
#include <openlcb/MultiConfiguredPC.hxx>
#include <openlcb/RefreshLoop.hxx>
#include <openlcb/SimpleStack.hxx>
#include <utils/AutoSyncFileFlow.hxx>
#include <utils/format_utils.hxx>
#include <utils/constants.hxx>

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

// Configurable IO pins
constexpr const Gpio *const CONFIGURABLE_GPIO_PIN_INSTANCES[] =
{
    IO1_Pin::instance(),  IO2_Pin::instance(),  IO3_Pin::instance()
  , IO4_Pin::instance(),  IO5_Pin::instance(),  IO6_Pin::instance()
  , IO7_Pin::instance(),  IO8_Pin::instance()
  , IO11_Pin::instance(), IO12_Pin::instance(), IO13_Pin::instance()
  , IO14_Pin::instance(), IO15_Pin::instance(), IO16_Pin::instance()
};

// TODO: shift to SimpleStackBase to allow shift to TCP/IP native stack rather
// than GridConnect TCP/IP stack.
openlcb::SimpleCanStack stack(CONFIG_OLCB_NODE_ID);
static int config_fd;
FactoryResetHelper factory_reset_helper(cfg);
esp32io::DelayRebootHelper delayed_reboot(stack.service());
esp32io::HealthMonitor health_mon(stack.service());
openlcb::ConfiguredProducer gpi_9(stack.node(), cfg.seg().gpi().entry<0>()
                                , IO9_Pin::instance());
openlcb::ConfiguredProducer gpi_10(stack.node(), cfg.seg().gpi().entry<1>()
                                 , IO10_Pin::instance());
openlcb::MultiConfiguredPC multi_pc(stack.node(), CONFIGURABLE_GPIO_PIN_INSTANCES
                                  , ARRAYSIZE(CONFIGURABLE_GPIO_PIN_INSTANCES)
                                  , cfg.seg().gpio());
openlcb::RefreshLoop refresh_loop(stack.node()
                                , { gpi_9.polling()
                                  , gpi_10.polling()
                                  , multi_pc.polling()});
std::unique_ptr<Esp32WiFiManager> wifi_manager;
std::unique_ptr<AutoSyncFileFlow> config_sync;
std::unique_ptr<esp32io::NodeRebootHelper> node_reboot_helper;

Esp32Twai twai("/dev/twai", CONFIG_TWAI_RX_PIN, CONFIG_TWAI_TX_PIN);

static void twai_init_task(void *param)
{
  twai.hw_init();
  stack.add_can_port_select("/dev/twai/twai0");
  vTaskDelete(nullptr);
}

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
    stack.factory_reset_all_events(cfg.seg().internal_config()
                                    , CONFIG_OLCB_NODE_ID, config_fd);
    fsync(config_fd);
}

openlcb::SimpleCanStack *prepare_openlcb_stack(node_config_t *config, bool reset_events)
{
    stack.set_tx_activity_led(LED_ACTIVITY_Pin::instance());

    // If the wifi mode is enabled start the wifi manager and httpd.
    if (config->wifi_mode > WIFI_MODE_NULL && config->wifi_mode < WIFI_MODE_MAX)
    {
        // if the wifi mode is not SoftAP and we do not have a station SSID
        // force reset to SoftAP only.
        if (config->wifi_mode != WIFI_MODE_AP && strlen(config->sta_ssid) == 0)
        {
            reset_wifi_config_to_softap(config);
        }

        wifi_manager.reset(
            new Esp32WiFiManager(config->wifi_mode != WIFI_MODE_AP ? config->sta_ssid : config->ap_ssid
                               , config->wifi_mode != WIFI_MODE_AP ? config->sta_pass : config->ap_pass
                               , &stack
                               , cfg.seg().wifi()
                               , config->hostname_prefix
                               , config->wifi_mode
                               , nullptr // TODO add config.sta_ip
                               , ip_addr_any
                               , 1
                               , config->ap_auth
                               , config->ap_ssid
                               , config->ap_pass));

        wifi_manager->wait_for_ssid_connect(config->sta_wait_for_connect);
        wifi_manager->register_network_up_callback(
        [](esp_interface_t iface, uint32_t ip)
        {
            LED_WIFI_Pin::set(true);
        });
        wifi_manager->register_network_down_callback(
        [](esp_interface_t iface)
        {
            LED_WIFI_Pin::set(false);
        });
#ifdef CONFIG_TIMEZONE
        LOG(INFO, "[TimeZone] %s", CONFIG_TIMEZONE);
        setenv("TZ", CONFIG_TIMEZONE, 1);
        tzset();
#endif // CONFIG_TIMEZONE
#if CONFIG_SNTP
        if (config->wifi_mode == WIFI_MODE_APSTA ||
            config->wifi_mode == WIFI_MODE_STA)
        {
            LOG(INFO, "[SNTP] Polling %s for time updates", CONFIG_SNTP_SERVER);
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, CONFIG_SNTP_SERVER);
            sntp_set_time_sync_notification_cb(sntp_received);
            sntp_init();
        }
#endif // CONFIG_SNTP
    }

#if CONFIG_OLCB_PRINT_ALL_PACKETS
    stack.print_all_packets();
#endif

    // Initialize the TWAI driver from core 1 to ensure the TWAI driver is
    // tied to the core that the OpenMRN stack is *NOT* running on.
    xTaskCreatePinnedToCore(twai_init_task, "twai-init", 2048, nullptr
                          , config_arduino_openmrn_task_priority(), nullptr
                          , APP_CPU_NUM);

    // Create / update CDI, if the CDI is out of date a factory reset will be
    // forced.
    if (CDIHelper::create_config_descriptor_xml(cfg, openlcb::CDI_FILE
                                              , &stack))
    {
        LOG(WARNING, "[CDI] Forcing factory reset due to CDI update");
        unlink(openlcb::CONFIG_FILENAME);
    }

    // Create config file and initiate factory reset if it doesn't exist or is
    // otherwise corrupted.
    config_fd =
        stack.create_config_file_if_needed(cfg.seg().internal_config()
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
    config_sync.reset(
        new AutoSyncFileFlow(stack.service(), config_fd
                           , SEC_TO_USEC(CONFIG_OLCB_CONFIG_SYNC_SEC)));

    // Configure the node reboot helper to allow safe shutdown of file handles
    // and file systems etc.
    node_reboot_helper.reset(
        new esp32io::NodeRebootHelper(&stack, config_fd, config_sync.get()));

    // Initialize the webserver after the config file has been created/opened.
    if (config->wifi_mode > WIFI_MODE_NULL && config->wifi_mode < WIFI_MODE_MAX)
    {
        init_webserver(config, config_fd, &stack);
    }

    return &stack;
}