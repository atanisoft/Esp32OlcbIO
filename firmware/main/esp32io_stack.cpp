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
 * OpenMRN Stack stack initialization.
 *
 * @author Mike Dunston
 * @date 1 September 2020
 */

#include "sdkconfig.h"
#include "cdi.hxx"
#include "DelayRebootHelper.hxx"
#include "Esp32PCA9685PWM.hxx"
#include "EventBroadcastHelper.hxx"
#include "FactoryResetHelper.hxx"
#include "fs.hxx"
#include "hardware.hxx"
#include "HealthMonitor.hxx"
#include "NodeRebootHelper.hxx"
#include "nvs_config.hxx"
#include "web_server.hxx"

#include <freertos_drivers/esp32/Esp32HardwareTwai.hxx>
#include <freertos_drivers/esp32/Esp32WiFiManager.hxx>
#include <openlcb/MemoryConfigClient.hxx>
#include <openlcb/RefreshLoop.hxx>
#include <openlcb/ServoConsumer.hxx>
#include <openlcb/SimpleStack.hxx>
#include <utils/constants.hxx>
#include <utils/format_utils.hxx>
#include <utils/Uninitialized.hxx>

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

namespace esp32io
{

ConfigDef cfg(0);
int config_fd;
uninitialized<openlcb::SimpleCanStack> stack;
uninitialized<Esp32WiFiManager> wifi_manager;
uninitialized<openlcb::MemoryConfigClient> memory_client;
uninitialized<FactoryResetHelper> factory_reset_helper;
uninitialized<EventBroadcastHelper> event_helper;
uninitialized<DelayRebootHelper> delayed_reboot;
uninitialized<HealthMonitor> health_mon;
uninitialized<NodeRebootHelper> node_reboot_helper;
uninitialized<openlcb::ConfiguredProducer> inputs[ARRAYSIZE(INPUT_ONLY_GPIO)];
uninitialized<openlcb::MultiConfiguredPC> multi_pc;
std::unique_ptr<openlcb::RefreshLoop> refresh_loop;
#if CONFIG_OLCB_ENABLE_TWAI
Esp32HardwareTwai twai(CONFIG_TWAI_RX_PIN, CONFIG_TWAI_TX_PIN);
#endif // CONFIG_OLCB_ENABLE_TWAI
#if CONFIG_OLCB_ENABLE_PWM
Esp32PCA9685PWM pca9685(CONFIG_SDA_PIN, CONFIG_SCL_PIN, PCA9685_ADDR, 1000);
uninitialized<Esp32PCA9685PWMBit> pca9685PWM[16];
uninitialized<openlcb::ServoConsumer> servos[16];
#endif // CONFIG_OLCB_ENABLE_PWM

void factory_reset_events()
{
    LOG(WARNING, "[CDI] Resetting event IDs");
    stack->factory_reset_all_events(cfg.seg().internal_config()
                                  , stack->node()->node_id(), config_fd);
    fsync(config_fd);
}

void NodeRebootHelper::reboot()
{
    // make sure we are not called from the executor thread otherwise there
    // will be a deadlock
    HASSERT(os_thread_self() != stack->executor()->thread_handle());
    shutdown_webserver();
    LOG(INFO, "[Reboot] Shutting down LCC executor...");
    stack->executor()->sync_run([&]()
    {
        close(config_fd);
        unmount_fs();
        // restart the node
        LOG(INFO, "[Reboot] Restarting!");
        esp_restart();
    });
}

void EventBroadcastHelper::send_event(uint64_t eventID)
{
    stack->executor()->add(new CallbackExecutable([&]()
    {
        stack->send_event(eventID);
    }));
}

template<const unsigned num, const char separator>
void inject_seperator(std::string & input)
{
    for (auto it = input.begin(); (num + 1) <= std::distance(it, input.end());
        ++it)
    {
        std::advance(it, num);
        it = input.insert(it, separator);
    }
}

ConfigUpdateListener::UpdateAction FactoryResetHelper::apply_configuration(
    int fd, bool initial_load, BarrierNotifiable *done)
{
    // nothing to do here as we do not load config
    AutoNotify n(done);
    LOG(VERBOSE, "[CFG] apply_configuration(%d, %d)", fd, initial_load);

    return ConfigUpdateListener::UpdateAction::UPDATED;
}

void FactoryResetHelper::factory_reset(int fd)
{
    LOG(VERBOSE, "[CFG] factory_reset(%d)", fd);
    // set the name of the node to the SNIP model name
    cfg.userinfo().name().write(fd, openlcb::SNIP_STATIC_DATA.model_name);
    string node_id = uint64_to_string_hex(stack->node()->node_id(), 12);
    std::replace(node_id.begin(), node_id.end(), ' ', '0');
    inject_seperator<2, '.'>(node_id);
    // set the node description to the node id in expanded hex format.
    cfg.userinfo().description().write(fd, node_id.c_str());

    // set the default names of the input only pins.
    auto inputs = cfg.seg().gpi();
    for (size_t idx = 0; idx < ARRAYSIZE(INPUT_ONLY_GPIO_NAMES); idx++)
    {
        inputs.entry(idx).description().write(fd, INPUT_ONLY_GPIO_NAMES[idx]);
    }
    // set the default names of the configurable pins.
    auto config_io = cfg.seg().gpio();
    for (size_t idx = 0; idx < ARRAYSIZE(CONFIGURABLE_GPIO_NAMES); idx++)
    {
        config_io.entry(idx).pc().description().write(fd, CONFIGURABLE_GPIO_NAMES[idx]);
    }
}

#ifndef CONFIG_WIFI_STATION_SSID
#define CONFIG_WIFI_STATION_SSID ""
#endif

#ifndef CONFIG_WIFI_STATION_PASSWORD
#define CONFIG_WIFI_STATION_PASSWORD ""
#endif

#ifndef CONFIG_WIFI_SOFTAP_SSID
#define CONFIG_WIFI_SOFTAP_SSID "esp32olcbio"
#endif

#ifndef CONFIG_WIFI_SOFTAP_PASSWORD
#define CONFIG_WIFI_SOFTAP_PASSWORD "esp32olcbio"
#endif

#ifndef CONFIG_WIFI_HOSTNAME_PREFIX
#define CONFIG_WIFI_HOSTNAME_PREFIX "esp32io_"
#endif

#ifndef CONFIG_WIFI_SOFTAP_CHANNEL
#define CONFIG_WIFI_SOFTAP_CHANNEL 1
#endif

#ifndef CONFIG_SNTP_SERVER
#define CONFIG_SNTP_SERVER "pool.ntp.org"
#endif

#ifndef CONFIG_TIMEZONE
#define CONFIG_TIMEZONE "UTC0"
#endif

void start_openlcb_stack(node_config_t *config, bool reset_events
                       , bool brownout_detected, bool wifi_verbose)
{
    LOG(INFO, "[SNIP] version:%d, manufacturer:%s, model:%s, hw-v:%s, sw-v:%s"
      , openlcb::SNIP_STATIC_DATA.version
      , openlcb::SNIP_STATIC_DATA.manufacturer_name
      , openlcb::SNIP_STATIC_DATA.model_name
      , openlcb::SNIP_STATIC_DATA.hardware_version
      , openlcb::SNIP_STATIC_DATA.software_version);
    stack.emplace(config->node_id);
    stack->set_tx_activity_led(LED_ACTIVITY_Pin::instance());
#if CONFIG_OLCB_PRINT_ALL_PACKETS
    stack->print_all_packets();
#endif

    memory_client.emplace(stack->node(), stack->memory_config_handler());
    wifi_manager.emplace(stack.get_mutable(), cfg.seg().wifi()
                       , (wifi_mode_t)CONFIG_WIFI_MODE
                       , CONFIG_WIFI_HOSTNAME_PREFIX
                       , CONFIG_WIFI_STATION_SSID, CONFIG_WIFI_STATION_PASSWORD
                       , nullptr /* Station static ip */
                       , ip_addr_any /* dns entry */
                       , CONFIG_WIFI_SOFTAP_SSID, CONFIG_WIFI_SOFTAP_PASSWORD
                       , CONFIG_WIFI_SOFTAP_CHANNEL
                       , nullptr /* SoftAP static ip */
                       , CONFIG_SNTP_SERVER, CONFIG_TIMEZONE
                       , false);
    wifi_manager->set_status_led(LED_WIFI_Pin::instance());
    if (wifi_verbose)
    {
        wifi_manager->enable_verbose_logging();
    }
    init_webserver(memory_client.get_mutable(), stack->service(), config->node_id);
    factory_reset_helper.emplace();
    event_helper.emplace();
    delayed_reboot.emplace(stack->service());
    health_mon.emplace(stack->service());
    node_reboot_helper.emplace();

    for (size_t idx = 0; idx < ARRAYSIZE(INPUT_ONLY_GPIO); idx++)
    {
        inputs[idx].emplace(stack->node(), cfg.seg().gpi().entry(idx)
                          , INPUT_ONLY_GPIO[idx]);
    }
    multi_pc.emplace(stack->node(), CONFIGURABLE_GPIO
                   , ARRAYSIZE(CONFIGURABLE_GPIO), cfg.seg().gpio());
    refresh_loop.reset(
        new openlcb::RefreshLoop(stack->node()
                               , { inputs[0]->polling(), inputs[1]->polling()
                                 , inputs[2]->polling(), inputs[3]->polling()
                                 , multi_pc->polling() }));
    if (brownout_detected)
    {
        // Queue the brownout event to be sent.
        LOG_ERROR("[Brownout] Detected a brownout reset, sending event");
        event_helper->send_event(openlcb::Defs::NODE_POWER_BROWNOUT_EVENT);
    }

#if CONFIG_OLCB_ENABLE_TWAI
    stack->executor()->add(new CallbackExecutable([]
    {
        // Initialize the TWAI driver
        twai.hw_init();
        stack->add_can_port_async("/dev/twai/twai0");
    }));
#endif // CONFIG_OLCB_ENABLE_TWAI

#if CONFIG_OLCB_ENABLE_PWM
    LOG(VERBOSE, "Initializing PCA9685");
    pca9685.hw_init("pca9685");
    for (size_t idx = 0; idx < 16; idx++)
    {
        LOG(VERBOSE, "Creating ServoConsumer(%zu)", idx);
        pca9685PWM[idx].emplace(&pca9685, idx);
        servos[idx].emplace(stack->node(), cfg.seg().pwm().entry(idx)
                          , CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000ULL
                          , pca9685PWM[idx].get_mutable());
    }
#endif // CONFIG_OLCB_ENABLE_PWM

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

    // Start the stack in the background using it's own task.
    stack->start_executor_thread("OpenMRN"
                               , config_arduino_openmrn_task_priority()
                               , config_arduino_openmrn_stack_size());
}

} // namespace esp32io

namespace openlcb
{
    // Path to where OpenMRN should persist general configuration data.
    const char *const CONFIG_FILENAME = "/fs/config";

    // The size of the memory space to export over the above device.
    const size_t CONFIG_FILE_SIZE =
        esp32io::cfg.seg().size() + esp32io::cfg.seg().offset();

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
    extern const char CDI_DATA[];
    const char CDI_DATA[] = R"xmlpayload(<?xml version="1.0"?>
<cdi xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://openlcb.org/schema/cdi/1/1/cdi.xsd">
<identification>
<manufacturer>http://atanisoft.github.io/esp32olcbio</manufacturer>)xmlpayload"
#if CONFIG_OLCB_ENABLE_PWM
R"xmlpayload(<model>Esp32OlcbIO + PWM</model>)xmlpayload"
#else
R"xmlpayload(<model>Esp32OlcbIO</model>)xmlpayload"
#endif
R"xmlpayload(<hardwareVersion>)xmlpayload" SNIP_HW_VERSION R"xmlpayload(</hardwareVersion>
<softwareVersion>)xmlpayload" SNIP_SW_VERSION R"xmlpayload(</softwareVersion>
</identification>
<acdi/>
<segment space='251' origin='1'>
<string size='63'>
<name>User Name</name>
<description>This name will appear in network browsers for the current node.</description>
</string>
<string size='64'>
<name>User Description</name>
<description>This description will appear in network browsers for the current node.</description>
</string>
</segment>
<segment space='253' origin='128'>
<group>
<name>Internal data</name>
<description>Do not change these settings.</description>
<int size='2'>
<name>Version</name>
</int>
<int size='2'>
<name>Next event ID</name>
</int>
</group>
<group>
<name>WiFi Configuration</name>
<int size='1'>
<name>WiFi mode</name>
<description>Configures the WiFi operating mode.</description>
<min>0</min>
<max>3</max>
<default>2</default>
<map><relation><property>0</property><value>Off</value></relation><relation><property>1</property><value>Station Only</value></relation><relation><property>2</property><value>SoftAP Only</value></relation><relation><property>3</property><value>SoftAP and Station</value></relation></map>
</int>
<string size='21'>
<name>Hostname prefix</name>
<description>Configures the hostname prefix used by the node.
Note: the node ID will be appended to this value.</description>
</string>
<string size='32'>
<name>Station SSID</name>
<description>Configures the SSID that the ESP32 will connect to.</description>
</string>
<string size='128'>
<name>Station password</name>
<description>Configures the password that the ESP32 will use for the station SSID.</description>
</string>
<string size='32'>
<name>SoftAP SSID</name>
<description>Configures the SSID that the ESP32 will use for the SoftAP.</description>
</string>
<string size='128'>
<name>SoftAP assword</name>
<description>Configures the password that the ESP32 will use for the SoftAP.</description>
</string>
<int size='1'>
<name>Authentication Mode</name>
<description>Configures the authentication mode of the SoftAP.</description>
<min>0</min>
<max>7</max>
<default>3</default>
<map><relation><property>0</property><value>Open</value></relation><relation><property>1</property><value>WEP</value></relation><relation><property>2</property><value>WPA</value></relation><relation><property>3</property><value>WPA2</value></relation><relation><property>4</property><value>WPA/WPA2</value></relation></map>
</int>
<int size='1'>
<name>WiFi Channel</name>
<description>Configures the WiFi channel to use for the SoftAP.
Note: Some channels overlap eachother and may not provide optimal performance.Recommended channels are: 1, 6, 11 since these do not overlap.</description>
<min>1</min>
<max>14</max>
<default>1</default>
</int>
<int size='1'>
<name>Enable SNTP</name>
<description>Enabling this option will allow the ESP32 to poll an SNTP server at regular intervals to obtain the current time. The refresh interval roughly once per hour.</description>
<min>0</min>
<max>1</max>
<default>0</default>
<map><relation><property>0</property><value>Disabled</value></relation><relation><property>1</property><value>Enabled</value></relation></map>
</int>
<string size='64'>
<name>SNTP Server</name>
<description>Enter the SNTP Server address. Example: pool.ntp.org
Most of the time this does not need to be changed.</description>
</string>
<string size='64'>
<name>TimeZone</name>
<description>This is the timezone that the ESP32 should use, note it must be in POSIX notation. Note: The timezone is only configured when SNTP is also enabled.
A few common values:
PST8PDT,M3.2.0,M11.1.0 -- UTC-8 with automatic DST adjustment
MST7MDT,M3.2.0,M11.1.0 -- UTC-7 with automatic DST adjustment
CST6CDT,M3.2.0,M11.1.0 -- UTC-6 with automatic DST adjustment
EST5EDT,M3.2.0,M11.1.0 -- UTC-5 with automatic DST adjustment
A complete list can be seen here in the second column:
https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv</description>
</string>
<group>
<name>Hub Configuration</name>
<description>Configuration settings for an OpenLCB Hub</description>
<int size='1'>
<name>Enable</name>
<description>Configures this node as an OpenLCB hub which can accept connections from other nodes.
NOTE: This may cause some instability as the number of connected nodes increases.</description>
<min>0</min>
<max>1</max>
<default>0</default>
<map><relation><property>0</property><value>Disabled</value></relation><relation><property>1</property><value>Enabled</value></relation></map>
</int>
<int size='2'>
<name>Hub Listener Port</name>
<description>Defines the TCP/IP listener port this node will use when operating as a hub. Most of the time this does not need to be changed.</description>
<min>1</min>
<max>65535</max>
<default>12021</default>
</int>
<string size='64'>
<name>mDNS Service</name>
<description>mDNS or Bonjour service name, such as _openlcb-can._tcp</description>
</string>
<group offset='6'/>
</group>
<group>
<name>Uplink Configuration</name>
<description>Configures how this node will connect to other nodes.</description>
<int size='1'>
<name>Enable</name>
<description>Enables connecting to an OpenLCB Hub. In some cases it may be desirable to disable the uplink, such as a CAN only configuration.</description>
<min>0</min>
<max>1</max>
<default>1</default>
<map><relation><property>0</property><value>Disabled</value></relation><relation><property>1</property><value>Enabled</value></relation></map>
</int>
<string size='64'>
<name>mDNS Service</name>
<description>mDNS or Bonjour service name, such as _openlcb-can._tcp</description>
</string>
<string size='64'>
<name>IP Address</name>
<description>Enter the server IP address. Example: 192.168.0.55
Note: This will be used as a fallback when mDNS lookup is not successful.</description>
</string>
<int size='2'>
<name>Port Number</name>
<description>TCP port number of the server. Most of the time this does not need to be changed.</description>
<min>1</min>
<max>65535</max>
<default>12021</default>
</int>
</group>
<int size='1'>
<name>WiFi Power Savings Mode</name>
<description>When enabled this allows the ESP32 WiFi radio to use power savings mode which puts the radio to sleep except to receive beacon updates from the connected SSID. This should generally not need to be enabled unless you are powering the ESP32 from a battery.</description>
<min>0</min>
<max>1</max>
<default>0</default>
<map><relation><property>0</property><value>Disabled</value></relation><relation><property>1</property><value>Enabled</value></relation></map>
</int>
<int size='1'>
<name>WiFi Transmit Power</name>
<description>WiFi Radio transmit power in dBm. This can be used to limit the WiFi range. This option generally does not need to be changed.
NOTE: Setting this option to a very low value can cause communication failures.</description>
<min>8</min>
<max>78</max>
<default>78</default>
<map><relation><property>8</property><value>2 dBm</value></relation><relation><property>20</property><value>5 dBm</value></relation><relation><property>28</property><value>7 dBm</value></relation><relation><property>34</property><value>8 dBm</value></relation><relation><property>44</property><value>11 dBm</value></relation><relation><property>52</property><value>13 dBm</value></relation><relation><property>56</property><value>14 dBm</value></relation><relation><property>60</property><value>15 dBm</value></relation><relation><property>66</property><value>16 dBm</value></relation><relation><property>72</property><value>18 dBm</value></relation><relation><property>78</property><value>20 dBm</value></relation></map>
</int>
<int size='1'>
<name>Wait for successful SSID connection</name>
<description>Enabling this option will cause the node to restart when there is a failure (or timeout) during the SSID connection process.</description>
<min>0</min>
<max>1</max>
<default>1</default>
<map><relation><property>0</property><value>Disabled</value></relation><relation><property>1</property><value>Enabled</value></relation></map>
</int>
</group>
<group replication='4'>
<name>Input Only Pins</name>
<repname>Input</repname>
<string size='15'>
<name>Description</name>
<description>User name of this input.</description>
</string>
<int size='1'>
<name>Debounce parameter</name>
<description>Amount of time to wait for the input to stabilize before producing the event. Unit is 30 msec of time. Usually a value of 2-3 works well in a non-noisy environment. In high noise (train wheels for example) a setting between 8 -- 15 makes for a slower response time but a more stable signal.
Formally, the parameter tells how many times of tries, each 30 msec apart, the input must have the same value in order for that value to be accepted and the event transition produced.</description>
<default>3</default>
</int>
<eventid>
<name>Event On</name>
<description>This event will be produced when the input goes to HIGH.</description>
</eventid>
<eventid>
<name>Event Off</name>
<description>This event will be produced when the input goes to LOW.</description>
</eventid>
</group>
<group replication='14'>
<name>Input Output Pins</name>
<repname>IO</repname>
<int size='1'>
<name>Configuration</name>
<default>1</default>
<map><relation><property>0</property><value>Output</value></relation><relation><property>1</property><value>Input</value></relation></map>
</int>
<int size='1'>
<name>Debounce parameter</name>
<description>Used for inputs only. Amount of time to wait for the input to stabilize before producing the event. Unit is 30 msec of time. Usually a value of 2-3 works well in a non-noisy environment. In high noise (train wheels for example) a setting between 8 -- 15 makes for a slower response time but a more stable signal.
Formally, the parameter tells how many times of tries, each 30 msec apart, the input must have the same value in order for that value to be accepted and the event transition produced.</description>
<default>3</default>
</int>
<group offset='1'/>
<group>
<string size='20'>
<name>Description</name>
<description>User name of this line.</description>
</string>
<eventid>
<name>Event On</name>
<description>This event ID will turn the output on / be produced when the input goes on.</description>
</eventid>
<eventid>
<name>Event Off</name>
<description>This event ID will turn the output off / be produced when the input goes off.</description>
</eventid>
</group>
</group>
)xmlpayload"
#if CONFIG_OLCB_ENABLE_PWM
R"xmlpayload(
<group replication='16'>
<name>PWM</name>
<repname>PWM</repname>
<string size='16'>
<name>Description</name>
<description>User name of this output.</description>
</string>
<eventid>
<name>Minimum Rotation Event ID</name>
<description>Receiving this event ID will rotate the servo to its mimimum configured point.</description>
</eventid>
<eventid>
<name>Maximum Rotation Event ID</name>
<description>Receiving this event ID will rotate the servo to its maximum configured point.</description>
</eventid>
<int size='2'>
<name>Servo Minimum Stop Point Percentage</name>
<description>Low-end stop point of the servo, as a percentage: generally 0-100. May be under/over-driven by setting a percentage value of -99 to 200, respectively.</description>
<min>-99</min>
<max>200</max>
<default>0</default>
</int>
<int size='2'>
<name>Servo Maximum Stop Point Percentage</name>
<description>High-end stop point of the servo, as a percentage: generally 0-100. May be under/over-driven by setting a percentage value of -99 to 200, respectively.</description>
<min>-99</min>
<max>200</max>
<default>100</default>
</int>
</group>
)xmlpayload"
#endif // CONFIG_OLCB_ENABLE_PWM
R"xmlpayload(
</segment>
<segment space='253'>
<name>Version information</name>
<int size='1'>
<name>ACDI User Data version</name>
<description>Set to 2 and do not change.</description>
</int>
</segment>
</cdi>
)xmlpayload";
    extern const size_t CDI_SIZE;
    const size_t CDI_SIZE = sizeof(CDI_DATA);

    extern const uint16_t CDI_EVENT_OFFSETS[] =
    {
        828, 836, 860, 868, 892, 900, 924, 932, 963, 971, 1002, 1010, 1041,
        1049, 1080, 1088, 1119, 1127, 1158, 1166, 1197, 1205, 1236, 1244,
        1275, 1283, 1314, 1322, 1353, 1361, 1392, 1400, 1431, 1439, 1470, 1478,
#if CONFIG_OLCB_ENABLE_PWM
        1502, 1510, 1538, 1546, 1574, 1582, 1610, 1618, 1646, 1654, 1682, 1690,
        1718, 1726, 1754, 1762, 1790, 1798, 1826, 1834, 1862, 1870, 1898, 1906,
        1934, 1942, 1970, 1978, 2006, 2014, 2042, 2050,
#endif // CONFIG_OLCB_ENABLE_PWM
        0
    };


}