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
 * \file PCA9685PWM.hxx
 *
 * Implementation of a PCA9685 PWM I2C generator.
 *
 * @author Mike Dunston
 * @date 6 Feburary 2021
 */

#include <driver/i2c.h>
#include <esp_check.h>
#include <freertos_drivers/arduino/PWM.hxx>
#include <os/OS.hxx>
#include <sys/ioctl.h>
#include <utils/Atomic.hxx>

class PCA9685PWMBit;

/// Aggregate of 16 PWM channels for a PCA9685 I2C connected device.
class PCA9685PWM
{
public:
    /// Maximum number of PWM channels supported by the PCA9685.
    static constexpr size_t NUM_CHANNELS = 16;

    /// Maximum number of PWM counts supported by the PCA9685.
    static constexpr size_t MAX_PWM_COUNTS = 4096;

    /// Constructor.
    PCA9685PWM(uint8_t sda, uint8_t scl, uint8_t address, uint16_t frequency)
        : addr_(address)
        , sda_(static_cast<gpio_num_t>(sda))
        , scl_(static_cast<gpio_num_t>(scl))
        , frequency_(frequency)
    {
        duty_.fill(0);
    }

    /// Initialize device.
    /// @return ESP_OK if the hardware was initialized successfully, other
    /// values for failures.
    esp_err_t hw_init()
    {
        i2c_config_t i2c_config =
        {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = sda_,
            .scl_io_num = scl_,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master =
            {
                .clk_speed = I2C_BUS_SPEED
            },
            .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL
        };

        // ensure the PWM frequency is within normal range.
        if (frequency_ > (INTERNAL_CLOCK_FREQUENCY / (4096 * 4)))
        {
            ESP_LOGE(TAG, "[%02x] Invalid PWM frequency provided: %d", addr_,
                     frequency_);
            return ESP_ERR_INVALID_ARG;
        }

        LOG(INFO, "[PCA9685] Configuring I2C (scl:%d, sda:%d)", scl_, sda_);
        ESP_RETURN_ON_ERROR(i2c_param_config(I2C_PORT, &i2c_config), TAG,
            "Failed to configure I2C bus");
        ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0),
            TAG, "Failed to install I2C driver");

        if (ping_device(addr_) != ESP_OK)
        {
            // Scan the I2C bus and dump the output of devices that respond
            std::string scanresults =
                "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n"
                "00:         ";
            scanresults.reserve(256);
            for (uint8_t addr = 3; addr < 0x78; addr++)
            {
                if (addr % 16 == 0)
                {
                    scanresults += "\n" + int64_to_string_hex(addr) + ":";
                }
                esp_err_t ret = ping_device(addr);
                if (ret == ESP_OK)
                {
                    scanresults += int64_to_string_hex(addr);
                }
                else if (ret == ESP_ERR_TIMEOUT)
                {
                    scanresults += " ??";
                }
                else
                {
                    scanresults += " --";
                }
            }
            LOG(WARNING, "[PCA9685] I2C devices:\n%s", scanresults.c_str());
            return ESP_ERR_NOT_FOUND;    
        }

        MODE1_REGISTER mode1;
        mode1.auto_increment = 1;
        mode1.sleep = 1;
        mode1.all_call = 0;
        LOG(VERBOSE, "[%02x] Configuring MODE1 register: %02x", addr_,
            mode1.value);
        ESP_RETURN_ON_ERROR(register_write(REGISTERS::MODE1, mode1.value), TAG,
            "Failed to write MODE1 register");

        uint8_t prescaler =
            (INTERNAL_CLOCK_FREQUENCY / (4096 * frequency_)) - 1;
        ESP_LOGD(TAG, "[%02x] Configuring pre-scaler register: %d", addr_,
                 prescaler);
        ESP_RETURN_ON_ERROR(register_write(REGISTERS::PRE_SCALE, prescaler),
            TAG, "Failed to write PRESCALE register");

        /* if using internal clock */
        mode1.sleep = 0;
        ESP_RETURN_ON_ERROR(register_write(REGISTERS::MODE1, mode1.value), TAG,
            "Failed to write MODE1 register");

        MODE2_REGISTER mode2;
        mode2.output_check = 1;
        ESP_RETURN_ON_ERROR(register_write(REGISTERS::MODE2, mode2.value), TAG,
            "Failed to write MODE2 register");

        // Device is ready to use
        return ESP_OK;
    }

    /// Destructor.
    ~PCA9685PWM()
    {
    }

private:
    /// Log tag to use for this class.
    static constexpr const char *const TAG = "PCA9685";
    
    /// Default internal clock frequency, 25MHz.
    static constexpr uint32_t INTERNAL_CLOCK_FREQUENCY = 25000000;

    /// Maximum number of ticks to wait for an I2C transaction to complete.
    static constexpr TickType_t MAX_I2C_WAIT_TICKS = pdMS_TO_TICKS(100);

    /// I2C port to use.
    static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;

    /// I2C Bus speed.
    static constexpr uint32_t I2C_BUS_SPEED = 100000;

    /// I2C address of the device
    const uint8_t addr_;

    /// SDA pin to use for I2C communication.
    const gpio_num_t sda_;

    /// SCL pin to use for I2C communication.
    const gpio_num_t scl_;

    /// Desired PWM frequency.
    const uint16_t frequency_;

    /// Allow access to private members
    friend PCA9685PWMBit;

    /// local cache of the duty cycles
    std::array<uint16_t, NUM_CHANNELS> duty_;

    /// Device register offsets.
    enum REGISTERS
    {
        /// MODE1 register address.
        MODE1 = 0x00,

        /// MODE2 register address.
        MODE2 = 0x01,

        /// OUTPUT 0 first register address. This is used as a starting offset
        /// for all other output registers.
        LED0_ON_L = 0x6,

        /// Register address used to turn off all outputs.
        ALL_OFF = 0xFC,

        /// Clock pre-scaler divider register address.
        PRE_SCALE = 0xFE,
    };

    /// Mode1 register layout.
    union MODE1_REGISTER
    {
        /// Constructor
        MODE1_REGISTER() : value(0x01)
        {
        }
        /// Full byte value for the register
        uint8_t value;
        struct
        {
            uint8_t all_call        : 1;
            uint8_t sub_addr_3      : 1;
            uint8_t sub_addr_2      : 1;
            uint8_t sub_addr_1      : 1;
            uint8_t sleep           : 1;
            uint8_t auto_increment  : 1;
            uint8_t external_clock  : 1;
            uint8_t restart         : 1;
        };
    };

    /// Mode2 register layout.
    union MODE2_REGISTER
    {
        /// Constructor.
        MODE2_REGISTER() : value(0x04)
        {
        }

        /// Full byte value for the register
        uint8_t value;
        struct
        {
            uint8_t output_enable   : 2;
            uint8_t output_mode     : 1; // 1 = push/pull, 0 = open drain.
            uint8_t output_check    : 1; // 1 = update on ack, 0 = update on stop.
            uint8_t output_inverted : 1;
            uint8_t unused          : 3;
        };
    };

    /// Output channel register layout.
    struct OUTPUT_STATE_REGISTER
    {
        union On
        {
            /// Constructor.
            On() : value(0x0000)
            {
            }

            /// ON Register value
            uint16_t value;
            struct
            {
                /// Number of ON counts.
                uint16_t counts : 12;

                /// Set to full ON.
                uint16_t full_on : 1;

                /// Unused bits.
                uint16_t unused : 3;
            };
        };

        union Off
        {
            /// Constructor.
            Off() : value(0x0000)
            {
            }

            /// OFF Register value
            uint16_t value;
            struct
            {
                /// Number of OFF counts.
                uint16_t counts : 12;

                /// Set to full OF.
                uint16_t full_off : 1;

                /// Unused bits.
                uint16_t unused : 3;
            };
        };

        /// On register instance.
        On on;

        /// Off register instance.
        Off off;
    };


    /// Detect if the PCA9685 device is present or not.
    /// @param address I2C device address to ping.
    /// @return ESP_OK if the device responds, any other value indicates failure.
    esp_err_t ping_device(uint8_t address)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, MAX_I2C_WAIT_TICKS);
        i2c_cmd_link_delete(cmd);
        return err;
    }

    /// Set the pwm duty cycle
    /// @param channel channel index (0 through 15)
    /// @param counts counts for PWM duty cycle
    esp_err_t set_pwm_duty(size_t channel, uint16_t counts)
    {
        HASSERT(channel < NUM_CHANNELS);

        duty_[channel] = counts;
        return write_pwm_duty(channel, counts);
    }

    /// Get the pwm duty cycle
    /// @param channel channel index (0 through 15)
    /// @return counts for PWM duty cycle
    uint16_t get_pwm_duty(size_t channel)
    {
        HASSERT(channel < NUM_CHANNELS);
        return duty_[channel];
    }

    /// Write to an I2C register.
    /// @param reg Register to write to
    /// @param data data to write
    /// @return returns write status
    esp_err_t register_write(REGISTERS reg, uint8_t data)
    {
        uint8_t payload[] = {reg, data};
        return i2c_master_write_to_device(I2C_PORT, addr_, payload,
                                          sizeof(payload), MAX_I2C_WAIT_TICKS);
    }

    /// Write to multiple sequential I2C registers.
    /// @param reg Register to start write to
    /// @param data array of data to write
    /// @param count number of data registers to write in sequence
    /// @return returns write status
    esp_err_t register_write_multiple(REGISTERS reg, void *data, size_t count)
    {
        uint8_t payload[count + 1];
        payload[0] = addr_;
        memcpy(payload + 1, data, count);
        return i2c_master_write_to_device(I2C_PORT, addr_, payload,
                                          sizeof(payload), MAX_I2C_WAIT_TICKS);
    }

    /// Set the pwm duty cycle
    /// @param channel channel index (0 through 15)
    /// @param counts counts for PWM duty cycle
    esp_err_t write_pwm_duty(size_t channel, uint16_t counts)
    {

        OUTPUT_STATE_REGISTER reg_value;
        if (counts >= MAX_PWM_COUNTS)
        {
            reg_value.on.full_on = 1;
            reg_value.off.full_off = 0;
        }
        else if (counts == 0)
        {
            reg_value.on.full_on = 0;
            reg_value.off.full_off = 1;
        }
        else
        {
            // the "256" count offset is to help average the current accross
            // all 16 channels when the duty cycle is low.
            reg_value.on.counts = (channel * 256);
            reg_value.off.counts = (counts + (channel * 256)) % 0x1000;
        }
        REGISTERS output_register =
            (REGISTERS)(REGISTERS::LED0_ON_L + (channel << 2));
        ESP_LOGV(TAG, "[%02x:%d] Setting PWM to %d:%d", addr_, channel,
                 reg_value.on.value, reg_value.off.value);
        reg_value.on.value = htole16(reg_value.on.value);
        reg_value.off.value = htole16(reg_value.off.value);
        return register_write_multiple((REGISTERS)output_register, &reg_value,
                                       sizeof(OUTPUT_STATE_REGISTER));
    }

    DISALLOW_COPY_AND_ASSIGN(PCA9685PWM);
};

/// Specialization of the PWM abstract interface for the PCA9685
class PCA9685PWMBit : public PWM
{
public:
    /// Constructor.
    /// @param instance reference to the chip
    /// @param index channel index on the chip (0 through 15) 
    PCA9685PWMBit(PCA9685PWM *instance, size_t index)
        : PWM()
        , instance_(instance)
        , index_(index)
    {
        HASSERT(index < PCA9685PWM::NUM_CHANNELS);
    }

    /// Destructor.
    ~PCA9685PWMBit()
    {
    }

private:
    /// Set PWM period.
    /// @param PWM period in counts
    void set_period(uint32_t counts) override
    {
        HASSERT(counts == PCA9685PWM::MAX_PWM_COUNTS);
    };

    /// Get PWM period.
    /// @return PWM period in counts
    uint32_t get_period() override
    {
        return PCA9685PWM::MAX_PWM_COUNTS;
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
    PCA9685PWM *instance_;

    /// bit index within PCA9685
    size_t index_;

    DISALLOW_COPY_AND_ASSIGN(PCA9685PWMBit);
};

