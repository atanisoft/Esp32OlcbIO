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
 * \file Esp32PCA9685PWM.hxx
 *
 * Implementation of a PCA9685 PWM I2C generator.
 *
 * @author Mike Dunston
 * @date 6 Feburary 2021
 */

#include <driver/i2c.h>
#include <freertos_drivers/arduino/PWM.hxx>
#include <os/OS.hxx>
#include <sys/ioctl.h>
#include <utils/Atomic.hxx>

class Esp32PCA9685PWMBit;

/// Agragate of 16 PWM channels for a PCA9685PWM
class Esp32PCA9685PWM : public OSThread, private Atomic
{
public:
    /// maximum number of PWM channels supported by the PCA9685
    static constexpr size_t NUM_CHANNELS = 16;

    /// maximum number of PWM counts supported by the PCA9685
    static constexpr size_t MAX_PWM_COUNTS = 4096;

    /// Constructor.
    Esp32PCA9685PWM(uint8_t sda, uint8_t scl, uint8_t address, uint16_t frequency)
        : sem_(0)
        , dirty_(0)
        , i2cAddress_(address)
        , sda_(static_cast<gpio_num_t>(sda))
        , scl_(static_cast<gpio_num_t>(scl))
        , frequency_(frequency)
    {
        duty_.fill(0);
    }

    /// Initialize device.
    /// @param name name of the I2C device
    void hw_init(const char * name)
    {
        start(name, 0, 2048);
    }

    /// Destructor.
    ~Esp32PCA9685PWM()
    {
    }

private:
    /// User entry point for the created thread.
    /// @return exit status
    void *entry() override;

    /// Set the pwm duty cycle
    /// @param channel channel index (0 through 15)
    /// @param counts counts for PWM duty cycle
    void set_pwm_duty(unsigned channel, uint16_t counts);

    /// Get the pwm duty cycle
    /// @param channel channel index (0 through 15)
    /// @return counts for PWM duty cycle
    uint16_t get_pwm_duty(unsigned channel)
    {
        HASSERT(channel < NUM_CHANNELS);
        return duty_[channel];
    }

    /// Set the pwm duty cycle
    /// @param channel channel index (0 through 15)
    /// @param counts counts for PWM duty cycle
    void write_pwm_duty(unsigned channel, uint16_t counts);

    bool read_register(uint8_t reg, uint8_t *value);
    bool write_one_register(uint8_t reg, uint8_t value);
    bool write_one_register(uint8_t reg, uint16_t value);
    bool write_two_registers(uint8_t reg_base, uint16_t reg1_value, uint16_t reg2_value);

    /// Allow access to private members
    friend Esp32PCA9685PWMBit;

    /// Wakeup for the thread processing
    OSSem sem_;

    /// local cache of the duty cycles
    std::array<uint16_t, NUM_CHANNELS> duty_;

    /// set if the duty_ value is updated (bit mask)
    uint16_t dirty_;

    /// I2C address of the device
    const uint8_t i2cAddress_;
    const gpio_num_t sda_;
    const gpio_num_t scl_;
    const uint16_t frequency_;

    static constexpr uint8_t MODE1_RESET = 0x80;
    static constexpr uint8_t MODE1_SLEEP = 0x10;
    static constexpr uint8_t MODE1_AUTO_INCREMENT = 0xA0;

    static constexpr uint8_t MODE1_REG = 0x00;
    static constexpr uint8_t MODE2_REG = 0x01;
    static constexpr uint8_t LED_0_REG_ON = 0x06;
    static constexpr uint8_t LED_ALL_REG_ON = 0xFA;
    static constexpr uint8_t LED_ALL_REG_OFF = 0xFC;
    static constexpr uint8_t PRESCALE_REG = 0xFE;
    static constexpr float CLOCK_FREQUENCY = 25000000.0;
    static constexpr TickType_t MAX_I2C_WAIT_TICKS = pdMS_TO_TICKS(100);
    static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
    static constexpr uint32_t I2C_BUS_SPEED = 100000;

    DISALLOW_COPY_AND_ASSIGN(Esp32PCA9685PWM);
};

/// Specialization of the PWM abstract interface for the PCA9685
class Esp32PCA9685PWMBit : public PWM
{
public:
    /// Constructor.
    /// @param instance reference to the chip
    /// @param index channel index on the chip (0 through 15) 
    Esp32PCA9685PWMBit(Esp32PCA9685PWM *instance, unsigned index)
        : PWM()
        , instance_(instance)
        , index_(index)
    {
        HASSERT(index < Esp32PCA9685PWM::NUM_CHANNELS);
    }

    /// Destructor.
    ~Esp32PCA9685PWMBit()
    {
    }

private:
    /// Set PWM period.
    /// @param PWM period in counts
    void set_period(uint32_t counts) override
    {
        HASSERT(counts == Esp32PCA9685PWM::MAX_PWM_COUNTS);
    };

    /// Get PWM period.
    /// @return PWM period in counts
    uint32_t get_period() override
    {
        return Esp32PCA9685PWM::MAX_PWM_COUNTS;
    }

    /// Sets the duty cycle.
    /// @param counts duty cycle in counts
    void set_duty(uint32_t counts) override
    {
        instance_->set_pwm_duty(index_, counts);
    }

    /// Gets the duty cycle.
    /// @return counts duty cycle in counts
    uint32_t get_duty() override
    {
        return instance_->get_pwm_duty(index_);
    }

    /// Get max period supported
    /// @return period in counts
    uint32_t get_period_max() override
    {
        return get_period();
    }

    /// Get min period supported
    /// @return period in counts
    uint32_t get_period_min() override
    {
        return get_period();
    }

    /// instance pointer to the whole chip complement
    Esp32PCA9685PWM *instance_;

    /// bit index within PCA9685
    unsigned index_;

    DISALLOW_COPY_AND_ASSIGN(Esp32PCA9685PWMBit);
};

