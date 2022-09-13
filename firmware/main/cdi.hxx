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
/// Adds the versioning segment.
CDI_GROUP_ENTRY(version, VersionSeg);
CDI_GROUP_END();

} // namespace esp32olcbhub

namespace openlcb {
    extern const char CDI_DATA[];
    // This is a C++11 raw string.
    const char CDI_DATA[] = R"xmlpayload(<?xml version="1.0"?>
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
<description>This name will appear in network browsers for the current node.</description>
</string>
<string size='64'>
<name>User Description</name>
<description>This description will appear in network browsers for the current node.</description>
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
<name>SSID</name>
<description>Configures the SSID that the ESP32 will connect to.</description>
</string>
<string size='128'>
<name>Password</name>
<description>Configures the SSID that the ESP32 will connect to.</description>
</string>
<string size='32'>
<name>SSID</name>
<description>Configures the SSID that the ESP32 will use for the SoftAP.</description>
</string>
<string size='128'>
<name>Password</name>
<description>Configures the SSID that the ESP32 will use for the SoftAP.</description>
</string>
<int size='1'>
<name>Authentication Mode</name>
<description>Configures the authentication mode of the SoftAP.</description>
<min>0</min>
<max>7</max>
<default>3</default>
<map><relation><property>0</property><value>Open</value></relation><relation><property>1</property><value>WEP</value></relation><relation><property>2</property><value>WPA</value></relation><relation><property>3</property><value>WPA2</value></relation><relation><property>4</property><value>WPA/WPA2</value></relation><relation><property>6</property><value>WPA3</value></relation><relation><property>7</property><value>WPA2/WPA3</value></relation></map>
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
    
    828, 836,  // input 1
    860, 868,  // input 2
    892, 900,  // input 3
    924, 932,  // input 4
    
    963, 971,  // IO 1
    1002, 1010, // IO 2
    1041, 1049, // IO 3
    1080, 1088, // IO 4
    1119, 1127, // IO 5
    1158, 1166, // IO 6
    1197, 1205, // IO 7
    1236, 1244, // IO 8
    1275, 1283, // IO 9
    1314, 1322, // IO 10
    1353, 1361, // IO 11
    1392, 1400, // IO 12
    1431, 1439, // IO 13
    1470, 1478, // IO 14
#if CONFIG_OLCB_ENABLE_PWM
    1502, 1510, // SERVO 1
    1538, 1546, // SERVO 2
    1574, 1582, // SERVO 3
    1610, 1618, // SERVO 4
    1646, 1654, // SERVO 5
    1682, 1690, // SERVO 6
    1718, 1726, // SERVO 7
    1754, 1762, // SERVO 8
    1790, 1798, // SERVO 9
    1826, 1834, // SERVO 10
    1862, 1870, // SERVO 11
    1898, 1906, // SERVO 12
    1934, 1942, // SERVO 13
    1970, 1978, // SERVO 14
    2006, 2014, // SERVO 15
    2042, 2050, // SERVO 16
#endif // CONFIG_OLCB_ENABLE_PWM
    0 // end marker
    };
}  // namespace openlcb


#endif // CDI_HXX_