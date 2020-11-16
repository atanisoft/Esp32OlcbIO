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
 * \file hardware.hxx
 *
 * Hardware representation for the ESP32OlcbHub.
 *
 * @author Mike Dunston
 * @date 4 July 2020
 */

#ifndef HARDWARE_HXX_
#define HARDWARE_HXX_

#include <freertos_drivers/arduino/DummyGPIO.hxx>
#include <freertos_drivers/esp32/Esp32Gpio.hxx>
#include <os/Gpio.hxx>
#include <utils/GpioInitializer.hxx>

#include "sdkconfig.h"

/// Configurable IO Pin 1.
GPIO_PIN(IO1, GpioOutputSafeHigh, GPIO_NUM_18);

/// Configurable IO Pin 2.
GPIO_PIN(IO2, GpioOutputSafeHigh, GPIO_NUM_17);

/// Configurable IO Pin 3.
GPIO_PIN(IO3, GpioOutputSafeHigh, GPIO_NUM_16);

/// Configurable IO Pin 4.
GPIO_PIN(IO4, GpioOutputSafeHigh, GPIO_NUM_0);

/// Configurable IO Pin 5.
GPIO_PIN(IO5, GpioOutputSafeHigh, GPIO_NUM_2);

/// Configurable IO Pin 6.
GPIO_PIN(IO6, GpioOutputSafeHigh, GPIO_NUM_15);

/// Configurable IO Pin 7.
GPIO_PIN(IO7, GpioOutputSafeHigh, GPIO_NUM_12);

/// Configurable IO Pin 8.
GPIO_PIN(IO8, GpioOutputSafeHigh, GPIO_NUM_13);

/// Input only pin 9.
GPIO_PIN(IO9, GpioInputPU, GPIO_NUM_34);

/// Input only pin 10.
GPIO_PIN(IO10, GpioInputPU, GPIO_NUM_35);

/// Configurable IO Pin 11.
GPIO_PIN(IO11, GpioOutputSafeHigh, GPIO_NUM_32);

/// Configurable IO Pin 12.
GPIO_PIN(IO12, GpioOutputSafeHigh, GPIO_NUM_33);

/// Configurable IO Pin 13.
GPIO_PIN(IO13, GpioOutputSafeHigh, GPIO_NUM_25);

/// Configurable IO Pin 14.
GPIO_PIN(IO14, GpioOutputSafeHigh, GPIO_NUM_26);

/// Configurable IO Pin 15.
GPIO_PIN(IO15, GpioOutputSafeHigh, GPIO_NUM_27);

/// Configurable IO Pin 16.
GPIO_PIN(IO16, GpioOutputSafeHigh, GPIO_NUM_14);

/// Node Activity indicator LED. Active (ON) Low.
GPIO_PIN(LED_ACTIVITY, GpioOutputSafeHighInvert, GPIO_NUM_22);

/// WiFi Active indicator LED. Active (ON) Low.
GPIO_PIN(LED_WIFI, GpioOutputSafeHighInvert, GPIO_NUM_23);

/// Factory Reset Pin, pull LOW (GND) during startup to force reset of events
/// or all configuration based on how long it is held.
GPIO_PIN(FACTORY_RESET, GpioInputPU, GPIO_NUM_39);

/// User Button Pin.
GPIO_PIN(USER_BUTTON, GpioInputPU, GPIO_NUM_36);

/// GPIO Pin initializer.
typedef GpioInitializer<IO1_Pin,  IO2_Pin,  IO3_Pin,  IO4_Pin,
                        IO5_Pin,  IO6_Pin,  IO7_Pin,  IO8_Pin,
                        IO9_Pin,  IO10_Pin, IO11_Pin, IO12_Pin,
                        IO13_Pin, IO14_Pin, IO15_Pin, IO16_Pin,
                        LED_WIFI_Pin,       LED_ACTIVITY_Pin,
                        FACTORY_RESET_Pin,  USER_BUTTON_Pin> GpioInit;

/// GPIO Pin connected to the TWAI (CAN) Transceiver RX pin.
static constexpr gpio_num_t CONFIG_TWAI_RX_PIN = GPIO_NUM_4;

/// GPIO Pin connected to the TWAI (CAN) Transceiver TX pin.
static constexpr gpio_num_t CONFIG_TWAI_TX_PIN = GPIO_NUM_5;

/// GPIO Pin used for I2C SDA.
static constexpr gpio_num_t CONFIG_SDA_PIN = GPIO_NUM_19;

/// GPIO Pin used for I2C SCL.
static constexpr gpio_num_t CONFIG_SCL_PIN = GPIO_NUM_21;

#endif // HARDWARE_HXX_