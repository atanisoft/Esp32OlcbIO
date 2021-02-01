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

#if CONFIG_OLCB_ENABLE_PWM
#include <openlcb/ServoConsumerConfig.hxx>
#endif // CONFIG_OLCB_ENABLE_PWM

namespace esp32io
{

using INPUT_ONLY_PINS = openlcb::RepeatedGroup<openlcb::ProducerConfig, 4>;
using CONFIGURABLE_GPIO_PINS = openlcb::RepeatedGroup<openlcb::PCConfig, 14>;

#if CONFIG_OLCB_ENABLE_PWM
using PWM_PINS = openlcb::RepeatedGroup<openlcb::ServoConsumerConfig, 16>;
#endif // CONFIG_OLCB_ENABLE_PWM

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
#if CONFIG_OLCB_ENABLE_PWM
CDI_GROUP_ENTRY(pwm, PWM_PINS, Name("PWM"), RepName("PWM"));
#endif // CONFIG_OLCB_ENABLE_PWM
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
CDI_GROUP_ENTRY(userinfo, openlcb::UserInfoSegment);
/// Adds the main configuration segment.
CDI_GROUP_ENTRY(seg, IoBoard);
/// Adds the versioning segment.
CDI_GROUP_ENTRY(version, VersionSeg);
CDI_GROUP_END();

} // namespace esp32olcbhub

#endif // CDI_HXX_