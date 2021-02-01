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

/// Configurable IO Pin 1.
GPIO_PIN(IO1, GpioInputNP, GPIO_NUM_18);

/// Configurable IO Pin 2.
GPIO_PIN(IO2, GpioInputNP, GPIO_NUM_17);

/// Configurable IO Pin 3.
GPIO_PIN(IO3, GpioInputNP, GPIO_NUM_16);

/// Configurable IO Pin 4.
// ADC2_CHANNEL_0
GPIO_PIN(IO4, GpioInputNP, GPIO_NUM_0);

/// Configurable IO Pin 5.
// ADC2_CHANNEL_2
GPIO_PIN(IO5, GpioInputNP, GPIO_NUM_2);

/// Configurable IO Pin 6.
// ADC2_CHANNEL_3
GPIO_PIN(IO6, GpioInputNP, GPIO_NUM_15);

/// Configurable IO Pin 7.
// ADC2_CHANNEL_5
GPIO_PIN(IO7, GpioInputNP, GPIO_NUM_12);

/// Configurable IO Pin 8.
// ADC2_CHANNEL_4
GPIO_PIN(IO8, GpioInputNP, GPIO_NUM_13);

/// Input only pin 9. Pull-Up enabled by default.
// ADC1_CHANNEL_6
GPIO_PIN(IO9, GpioInputPU, GPIO_NUM_34);

/// Input only pin 10. Pull-Up enabled by default.
// ADC1_CHANNEL_7
GPIO_PIN(IO10, GpioInputPU, GPIO_NUM_35);

/// Configurable IO Pin 11.
// ADC1_CHANNEL_4
GPIO_PIN(IO11, GpioInputNP, GPIO_NUM_32);

/// Configurable IO Pin 12.
// ADC1_CHANNEL_5
GPIO_PIN(IO12, GpioInputNP, GPIO_NUM_33);

/// Configurable IO Pin 13.
// ADC2_CHANNEL_8
GPIO_PIN(IO13, GpioInputNP, GPIO_NUM_25);

/// Configurable IO Pin 14.
// ADC2_CHANNEL_9
GPIO_PIN(IO14, GpioInputNP, GPIO_NUM_26);

/// Configurable IO Pin 15.
// ADC2_CHANNEL_7
GPIO_PIN(IO15, GpioInputNP, GPIO_NUM_27);

/// Configurable IO Pin 16.
// ADC2_CHANNEL_6
GPIO_PIN(IO16, GpioInputNP, GPIO_NUM_14);

/// Node Activity indicator LED. Active (ON) Low.
GPIO_PIN(LED_ACTIVITY, GpioOutputSafeHighInvert, GPIO_NUM_22);

/// WiFi Active indicator LED. Active (ON) Low.
GPIO_PIN(LED_WIFI, GpioOutputSafeHighInvert, GPIO_NUM_23);

/// Factory Reset Pin, Pull-Up enabled by default.
// ADC1_CHANNEL_3
GPIO_PIN(FACTORY_RESET, GpioInputPU, GPIO_NUM_39);

/// User Button Pin. Pull-Up enabled by default.
// ADC1_CHANNEL_0
GPIO_PIN(USER_BUTTON, GpioInputPU, GPIO_NUM_36);

/// GPIO Pin initializer.
typedef GpioInitializer<IO1_Pin,  IO2_Pin,  IO3_Pin,  IO4_Pin,
                        IO5_Pin,  IO6_Pin,  IO7_Pin,  IO8_Pin,
                        IO9_Pin,  IO10_Pin, IO11_Pin, IO12_Pin,
                        IO13_Pin, IO14_Pin, IO15_Pin, IO16_Pin,
                        LED_WIFI_Pin,       LED_ACTIVITY_Pin,
                        FACTORY_RESET_Pin,  USER_BUTTON_Pin> GpioInit;

/// Configurable IO pins.
constexpr const Gpio *const CONFIGURABLE_GPIO[] =
{
    IO1_Pin::instance(),  IO2_Pin::instance(),  IO3_Pin::instance()
  , IO4_Pin::instance(),  IO5_Pin::instance(),  IO6_Pin::instance()
  , IO7_Pin::instance(),  IO8_Pin::instance()
  , IO11_Pin::instance(), IO12_Pin::instance(), IO13_Pin::instance()
  , IO14_Pin::instance(), IO15_Pin::instance(), IO16_Pin::instance()
};

/// Configurable IO pin names.
constexpr const char *const CONFIGURABLE_GPIO_NAMES[] =
{
  "IO 1",  "IO 2",  "IO 3",  "IO 4",  "IO 5",  "IO 6", "IO 7", "IO 8",
  "IO 11", "IO 12", "IO 13", "IO 14", "IO 15", "IO 16"
};

/// Input only pins.
constexpr const Gpio *const INPUT_ONLY_GPIO[] =
{
    FACTORY_RESET_Pin::instance(), USER_BUTTON_Pin::instance()
  , IO9_Pin::instance(),           IO10_Pin::instance()
};

/// Input only pin names.
constexpr const char *const INPUT_ONLY_GPIO_NAMES[] =
{
  "Factory Reset Button", "User Button", "Input 9", "Input 10"
};

/// GPIO Pin connected to the TWAI (CAN) Transceiver RX pin.
// ADC2_CHANNEL_0
static constexpr gpio_num_t CONFIG_TWAI_RX_PIN = GPIO_NUM_4;

/// GPIO Pin connected to the TWAI (CAN) Transceiver TX pin.
static constexpr gpio_num_t CONFIG_TWAI_TX_PIN = GPIO_NUM_5;

/// GPIO Pin used for I2C SDA.
static constexpr gpio_num_t CONFIG_SDA_PIN = GPIO_NUM_19;

/// GPIO Pin used for I2C SCL.
static constexpr gpio_num_t CONFIG_SCL_PIN = GPIO_NUM_21;

/// Default address for the PCA9685 PWM IC (all address pins to GND).
static constexpr uint8_t PCA9685_ADDR = 0x40;

#endif // HARDWARE_HXX_