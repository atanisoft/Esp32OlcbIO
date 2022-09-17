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
 * \file cdi.hxx
 *
 * Config representation for the ESP32 IO Board.
 *
 * @author Mike Dunston
 * @date 4 July 2020
 */

#ifndef CDI_HXX_
#define CDI_HXX_

#include "sdkconfig.h"

#include <freertos_drivers/esp32/Esp32WiFiConfiguration.hxx>
#include <openlcb/ConfigRepresentation.hxx>
#include <openlcb/ConfiguredProducer.hxx>
#include <openlcb/MultiConfiguredPC.hxx>
#include <openlcb/ServoConsumerConfig.hxx>

namespace esp32io
{

using INPUT_ONLY_PINS = openlcb::RepeatedGroup<openlcb::ProducerConfig, 4>;
using CONFIGURABLE_GPIO_PINS = openlcb::RepeatedGroup<openlcb::PCConfig, 14>;
using PWM_PINS = openlcb::RepeatedGroup<openlcb::ServoConsumerConfig, 16>;

/// Defines the main segment in the configuration CDI. This is laid out at
/// origin 128 to give space for the ACDI user data at the beginning.
CDI_GROUP(IoBoard, Segment(openlcb::MemoryConfigDefs::SPACE_CONFIG),
          Offset(128));
/// Each entry declares the name of the current entry, then the type and then
/// optional arguments list.
CDI_GROUP_ENTRY(internal_config, openlcb::InternalConfigData);
CDI_GROUP_ENTRY(wifi, WiFiConfiguration, Name("WiFi Configuration"));
CDI_GROUP_ENTRY(gpi, INPUT_ONLY_PINS, Name("Input Only Pins"),
                RepName("Input"));
CDI_GROUP_ENTRY(gpio, CONFIGURABLE_GPIO_PINS, Name("Input Output Pins"),
                RepName("IO"));
CDI_GROUP_ENTRY(pwm, PWM_PINS, Name("PWM"), RepName("PWM")
#if !CONFIG_OLCB_ENABLE_PWM
              , Hidden(true)
#endif // !CONFIG_OLCB_ENABLE_PWM
);
CDI_GROUP_END();

/// This segment is only needed temporarily until there is program code to set
/// the ACDI user data version byte.
CDI_GROUP(VersionSeg, Segment(openlcb::MemoryConfigDefs::SPACE_CONFIG),
    Name("Version information"));
CDI_GROUP_ENTRY(acdi_user_version, openlcb::Uint8ConfigEntry,
    Name("ACDI User Data version"), Description("Set to 2 and do not change."));
CDI_GROUP_END();

/// The main structure of the CDI. ConfigDef is the symbol we use in main.cxx
/// to refer to the configuration defined here.
CDI_GROUP(ConfigDef, MainCdi());
/// Adds the <identification> tag with the values from SNIP_STATIC_DATA above.
CDI_GROUP_ENTRY(ident, openlcb::Identification);
/// Adds an <acdi> tag.
CDI_GROUP_ENTRY(acdi, openlcb::Acdi);
/// Adds a segment for changing the values in the ACDI user-defined
/// space. UserInfoSegment is defined in the system header.
CDI_GROUP_ENTRY(userinfo, openlcb::UserInfoSegment, Name("User Info"));
/// Adds the main configuration segment.
CDI_GROUP_ENTRY(seg, IoBoard, Name("Settings"));
CDI_GROUP_END();

} // namespace esp32io


namespace openlcb
{
    extern const char CDI_DATA[];
    // This is a C++11 raw string.
    const char CDI_DATA[] = R"xmlpayload(<?xml version="1.0" encoding="utf-8"?>
<cdi xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://openlcb.org/schema/cdi/1/1/cdi.xsd">
<identification>
<manufacturer>)xmlpayload" SNIP_PROJECT_PAGE R"xmlpayload(</manufacturer>
<model>)xmlpayload" SNIP_PROJECT_NAME R"xmlpayload(</model>
<hardwareVersion>)xmlpayload" SNIP_HW_VERSION " " CONFIG_IDF_TARGET R"xmlpayload(</hardwareVersion>
<softwareVersion>)xmlpayload" SNIP_SW_VERSION R"xmlpayload(</softwareVersion>
</identification>
<acdi/>
<segment space='251' origin='1'>
<name>User Info</name>
<string size='63'>
<name>User Name</name>
<description>This name will appear in network browsers for this device.</description>
</string>
<string size='64'>
<name>User Description</name>
<description>This description will appear in network browsers for this device.</description>
</string>
</segment>
<segment space='253' origin='128'>
<name>Settings</name>
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
<name>WiFi Power Savings Mode</name>
<description>When enabled this allows the ESP32 WiFi radio to use power savings mode which puts the radio to sleep except to receive beacon updates from the connected SSID. This should generally not need to be enabled unless you are powering the ESP32 from a battery.</description>
<min>0</min>
<max>1</max>
<default>0</default>
<map><relation><property>0</property><value>No</value></relation><relation><property>1</property><value>Yes</value></relation></map>
</int>
<int size='1'>
<name>Connection Mode</name>
<description>Defines whether to allow accepting connections (according to the Hub configuration), making a connection (according to the Uplink configuration), or both.
This setting can be set to Disabled if the ESP32 will be using the TWAI (CAN) driver instead for the connection to other nodes.
Note: it is not recommended to enable the Hub functionality on single-core ESP32 models.</description>
<min>0</min>
<max>3</max>
<default>1</default>
<map><relation><property>0</property><value>Disabled</value></relation><relation><property>1</property><value>Uplink Only</value></relation><relation><property>2</property><value>Hub Only</value></relation><relation><property>3</property><value>Hub+Uplink</value></relation></map>
</int>
<group>
<name>Hub Configuration</name>
<description>Configuration settings for an OpenLCB Hub</description>
<int size='2'>
<name>Hub Listener Port</name>
<description>Defines the TCP/IP listener port this node will use when operating as a hub. Most of the time this does not need to be changed.</description>
<min>1</min>
<max>65535</max>
<default>12021</default>
</int>
<string size='48'>
<name>mDNS Service</name>
<description>mDNS or Bonjour service name, such as _openlcb-can._tcp</description>
</string>
<group offset='6'/>
</group>
<group>
<name>Node Uplink Configuration</name>
<description>Configures how this node will connect to other nodes.</description>
<int size='1'>
<name>Search Mode</name>
<description>Defines the order of how to locate the server to connect to. 'auto' uses the mDNS protocol to find the IP address automatically. 'manual' uses the IP address entered in this settings.</description>
<min>0</min>
<max>3</max>
<default>0</default>
<map><relation><property>0</property><value>Auto, Manual</value></relation><relation><property>1</property><value>Manual, Auto</value></relation><relation><property>2</property><value>Auto Only</value></relation><relation><property>3</property><value>Manual Only</value></relation></map>
</int>
<group>
<name>Manual Address</name>
<description>Set IP address here if auto-detection does not work.</description>
<string size='32'>
<name>IP Address</name>
<description>Enter the server IP address. Example: 192.168.0.55</description>
</string>
<int size='2'>
<name>Port Number</name>
<description>TCP port number of the server. Most of the time this does not need to be changed.</description>
<min>1</min>
<max>65535</max>
<default>12021</default>
</int>
</group>
<group>
<name>Auto Address</name>
<description>Advanced settings for the server IP address auto-detection (mDNS).</description>
<string size='48'>
<name>mDNS Service</name>
<description>mDNS or Bonjour service name, such as _openlcb-can._tcp</description>
</string>
<string size='48'>
<name>Only Hostname</name>
<description>Use when multiple servers provide the same service on the network. If set, selects this specific host name; the connection will fail if none of the servers have this hostname (use correct capitalization!). Example: My JMRI Railroad</description>
</string>
</group>
<int size='1'>
<name>Reconnect</name>
<description>If enabled, tries the last known good IP address before searching for the server.</description>
<min>0</min>
<max>1</max>
<default>1</default>
<map><relation><property>0</property><value>Disabled</value></relation><relation><property>1</property><value>Enabled</value></relation></map>
</int>
<group offset='34'/>
</group>
<group offset='6'/>
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
</group>)xmlpayload"
#if CONFIG_OLCB_ENABLE_PWM
R"xmlpayload(<group replication='16'>
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
</group>)xmlpayload"
#else
R"xmlpayload(<group offset='576'/>)xmlpayload"
#endif // CONFIG_OLCB_ENABLE_PWM
R"xmlpayload(</segment>
</cdi>)xmlpayload";
    extern const size_t CDI_SIZE;
    const size_t CDI_SIZE = sizeof(CDI_DATA);

    extern const uint16_t CDI_EVENT_OFFSETS[] =
    {
        378, 386,   // input 1
        410, 418,   // input 2
        442, 450,   // input 3
        474, 482,   // input 4

        513, 521,   // IO 1
        552, 560,   // IO 2
        591, 599,   // IO 3
        630, 638,   // IO 4
        669, 677,   // IO 5
        708, 716,   // IO 6
        747, 755,   // IO 7
        786, 794,   // IO 8
        825, 833,   // IO 9
        864, 872,   // IO 10
        903, 911,   // IO 11
        942, 950,   // IO 12
        981, 989,   // IO 13
        1020, 1028, // IO 14

        1052, 1060, // SERVO 1
        1088, 1096, // SERVO 2
        1124, 1132, // SERVO 3
        1160, 1168, // SERVO 4
        1196, 1204, // SERVO 5
        1232, 1240, // SERVO 6
        1268, 1276, // SERVO 7
        1304, 1312, // SERVO 8
        1340, 1348, // SERVO 9
        1376, 1384, // SERVO 10
        1412, 1420, // SERVO 11
        1448, 1456, // SERVO 12
        1484, 1492, // SERVO 13
        1520, 1528, // SERVO 14
        1556, 1564, // SERVO 15
        1592, 1600, // SERVO 16
        
        0           // end marker
    };
}  // namespace openlcb


#endif // CDI_HXX_