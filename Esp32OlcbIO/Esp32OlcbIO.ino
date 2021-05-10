/** \copyright
 * Copyright (c) 2021, Mike Dunston
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
 * \file ESP32OlcbIO.ino
 *
 * Main file for ESP32OlcbIO (minimal).
 *
 * @author Mike Dunston
 * @date 7 May 2021
 */

#include <Arduino.h>
#include <OpenMRNLite.h>
#include <SPIFFS.h>

#include "config.h"
#include "hardware.h"

// This is the node id to assign to this device, this must be unique
// on the CAN bus.
static constexpr uint64_t NODE_ID = UINT64_C(0x050201030000);

// This is the name of the WiFi network (access point) to connect to.
const char *ssid = "";

// Password of the wifi network.
const char *password = "";

// Uncomment this next line to enable the usage of WiFi.
#define ENABLE_WIFI

// Uncomment this next line to enable the usage of CAN.
#define ENABLE_CAN

// This is the primary entrypoint for the OpenMRN/LCC stack.
OpenMRN openmrn(NODE_ID);

// note the dummy string below is required due to a bug in the GCC compiler
// for the ESP32
string dummystring("abcdef");

// ConfigDef comes from config.h and is specific to this particular device and
// target. It defines the layout of the configuration memory space and is also
// used to generate the cdi.xml file. Here we instantiate the configuration
// layout. The argument of offset zero is ignored and will be removed later.
static constexpr esp32olcbio::ConfigDef cfg(0);

#ifdef ENABLE_WIFI
// This will manage the WiFi connection for the ESP32.
Esp32WiFiManager wifi_mgr(ssid, password, openmrn.stack(), cfg.seg().wifi());
#endif // ENABLE_WIFI

// Utility method for injecting a seperator character at regular intervals in a
// provided string.
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

// When the io board starts up the first time the config is blank and needs to
// be reset to factory settings.
class FactoryResetHelper : public DefaultConfigUpdateListener
{
public:
    FactoryResetHelper()
    {
    }

    UpdateAction apply_configuration(int fd, bool initial_load,
                                     BarrierNotifiable *done) override
    {
        // nothing to do here as we do not load config
        AutoNotify n(done);
        LOG(VERBOSE, "[CFG] apply_configuration(%d, %d)", fd, initial_load);

        return ConfigUpdateListener::UpdateAction::UPDATED;
    }

    void factory_reset(int fd) override
    {
        LOG(VERBOSE, "[CFG] factory_reset(%d)", fd);
        // set the name of the node to the SNIP model name
        cfg.userinfo().name().write(fd, openlcb::SNIP_STATIC_DATA.model_name);
        string node_id = uint64_to_string_hex(NODE_ID, 12);
        std::replace(node_id.begin(), node_id.end(), ' ', '0');
        inject_seperator<2, '.'>(node_id);
        // set the node description to the node id in expanded hex format.
        cfg.userinfo().description().write(fd, node_id.c_str());

        // set the default names of the input only pins.
        auto inputs = cfg.seg().gpi();
        for (size_t idx = 0; idx < ARRAYSIZE(INPUT_GPIO); idx++)
        {
            inputs.entry(idx).description().write(fd, INPUT_GPIO_NAMES[idx]);
        }
        // set the default names of the configurable pins.
        auto outputs = cfg.seg().gpo();
        for (size_t idx = 0; idx < ARRAYSIZE(OUTPUT_GPIO); idx++)
        {
            outputs.entry(idx).description().write(fd, OUTPUT_GPIO_NAMES[idx]);
        }
    }
} factory_reset_helper;

// Output GPIO event handlers.
openlcb::MultiConfiguredConsumer gpio_consumers(openmrn.stack()->node(),
    OUTPUT_GPIO, ARRAYSIZE(OUTPUT_GPIO), cfg.seg().gpo());

// Input GPIO event handlers.
openlcb::ConfiguredProducer IO9_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<0>(), IO9_Pin());
openlcb::ConfiguredProducer IO10_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<1>(), IO10_Pin());
openlcb::ConfiguredProducer IO11_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<2>(), IO11_Pin());
openlcb::ConfiguredProducer IO12_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<3>(), IO12_Pin());
openlcb::ConfiguredProducer IO13_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<4>(), IO13_Pin());
openlcb::ConfiguredProducer IO14_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<5>(), IO14_Pin());
openlcb::ConfiguredProducer IO15_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<6>(), IO15_Pin());
openlcb::ConfiguredProducer IO16_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<7>(), IO16_Pin());
openlcb::ConfiguredProducer FACTORY_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<8>(), FACTORY_RESET_Pin());
openlcb::ConfiguredProducer USER_producer(
    openmrn.stack()->node(), cfg.seg().gpi().entry<9>(), USER_BUTTON_Pin());

// Input polling mechanism.
openlcb::RefreshLoop producer_refresh_loop(openmrn.stack()->node(),
    {
        IO9_producer.polling(),
        IO10_producer.polling(),
        IO11_producer.polling(),
        IO12_producer.polling(),
        IO13_producer.polling(),
        IO14_producer.polling(),
        IO15_producer.polling(),
        IO16_producer.polling(),
        FACTORY_producer.polling(),
        USER_producer.polling(),
    }
);

namespace openlcb
{
    // Name of CDI.xml to generate dynamically.
    const char CDI_FILENAME[] = "/spiffs/cdi.xml";

    // This will stop openlcb from exporting the CDI memory space upon start.
    extern const char CDI_DATA[] = "";

    // Path to where OpenMRN should persist general configuration data.
    extern const char *const CONFIG_FILENAME = "/spiffs/openlcb_config";

    // The size of the memory space to export over the above device.
    extern const size_t CONFIG_FILE_SIZE =
        cfg.seg().size() + cfg.seg().offset();

    // Default to store the dynamic SNIP data is stored in the same persistant
    // data file as general configuration data.
    extern const char *const SNIP_DYNAMIC_FILENAME = CONFIG_FILENAME;

    // Defines the identification information for the node. The arguments are:
    //
    // - 4 (version info, always 4 by the standard
    // - Manufacturer name
    // - Model name
    // - Hardware version
    // - Software version
    //
    // This data will be used for all purposes of the identification:
    //
    // - the generated cdi.xml will include this data
    // - the Simple Node Ident Info Protocol will return this data
    // - the ACDI memory space will contain this data.
    extern const SimpleNodeStaticValues SNIP_STATIC_DATA =
    {
        4,
        "https://atanisoft.github.io/esp32olcbio",
        "Esp32OlcbIO",
        "1.0.0",
        "1.0.0"
    };
}

void setup()
{
    Serial.begin(115200L);

    // Initialize all declared GPIO pins
    GpioInit::hw_init();

    // Initialize the SPIFFS filesystem as our persistence layer
    if (!SPIFFS.begin())
    {
        printf("SPIFFS failed to mount, attempting to format and remount\n");
        if (!SPIFFS.begin(true))
        {
            printf("SPIFFS mount failed even with format, giving up!\n");
            LED_WIFI_Pin::instance()->set();
            while (1)
            {
                LED_WIFI_Pin::instance()->write(!LED_WIFI_Pin::instance()->read());
                LED_ACTIVITY_Pin::instance()->write(!LED_ACTIVITY_Pin::instance()->read());
                delay(500);
            }
        }
    }

    // turn off the LEDs by default
    LED_WIFI_Pin::instance()->clr();
    LED_ACTIVITY_Pin::instance()->clr();

    // Create the CDI.xml dynamically
    openmrn.create_config_descriptor_xml(cfg, openlcb::CDI_FILENAME);

    // Create the default internal configuration file
    openmrn.stack()->create_config_file_if_needed(cfg.seg().internal_config(),
        esp32olcbio::CANONICAL_VERSION, openlcb::CONFIG_FILE_SIZE);

    // Start the OpenMRN stack
    openmrn.begin();

    // Connect the WiFi LED to the WiFi Manager.
    wifi_mgr.set_status_led(LED_WIFI_Pin::instance());

    // Connect the status LED to the OpenMRN stack.
    openmrn.stack()->set_tx_activity_led(LED_ACTIVITY_Pin::instance());

    // Start a thread for OpenMRN to use.
    openmrn.start_executor_thread();

#ifdef ENABLE_CAN
    // Add the hardware CAN device to the OpenMRN stack.
    openmrn.add_can_port(
        new Esp32HardwareCan("esp32can", CONFIG_TWAI_RX_PIN, CONFIG_TWAI_TX_PIN));
#endif // ENABLE_CAN
}

void loop()
{
    // Call the OpenMRN executor, this needs to be done as often
    // as possible from the loop() method.
    openmrn.loop();
}
