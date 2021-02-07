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
 * \file Esp32PCA9685PWM.cpp
 *
 * Implementation of a PCA9685 PWM I2C generator.
 *
 * @author Mike Dunston
 * @date 6 Feburary 2021
 */

#include "Esp32PCA9685PWM.hxx"

#include <math.h>
#include <utils/logging.h>

void die_with(bool wifi, bool activity, unsigned period, bool toggle_both);

//
// entry()
//
void *Esp32PCA9685PWM::entry()
{
    i2c_config_t i2c_config;
    bzero(&i2c_config, sizeof(i2c_config_t));
    i2c_config.mode = I2C_MODE_MASTER;
    i2c_config.sda_io_num = sda_;
    i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.scl_io_num = scl_;
    i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.master.clk_speed = I2C_BUS_SPEED;
    LOG(INFO, "[PCA9685] Configuring I2C Master");
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    LOG(INFO, "[PCA9685] Verifying PCA9685 is present on address %d"
      , i2cAddress_);
    uint8_t value;
    if (!read_register(MODE1_REG, &value))
    {
        LOG_ERROR("[PCA9685] Device not responding?");
        die_with(true, false, 500, true);
    }

    LOG(INFO, "[PCA9685] Resetting PCA9685");
    write_one_register(MODE1_REG, MODE1_RESET);
    // allow time for the device to reset
    vTaskDelay(pdMS_TO_TICKS(50));

    LOG(INFO, "[PCA9685] Enabling PCA9685 Sleep mode");
    write_one_register(MODE1_REG, MODE1_SLEEP);
    LOG(INFO, "[PCA9685] Configuring PCA9685 Prescaler");
    uint8_t prescale_val =
        round((CLOCK_FREQUENCY / MAX_PWM_COUNTS / (0.9 * frequency_)) - 0.5);
    write_one_register(PRESCALE_REG, prescale_val);
    write_one_register(MODE1_REG, MODE1_RESET);
    // allow time for the device to reset
    vTaskDelay(pdMS_TO_TICKS(50));
    LOG(INFO, "[PCA9685] Enabling PCA9685 Auto Increment mode");
    write_one_register(MODE1_REG, MODE1_AUTO_INCREMENT);

    for ( ; /* forever */ ; )
    {
        sem_.wait();
        uint16_t dirty_shadow = 0;
        {
            AtomicHolder h(this);
            dirty_shadow = dirty_;
            dirty_ = 0;
        }

        for (unsigned i = 0; i < NUM_CHANNELS; ++i)
        {
            if (dirty_shadow & (0x1 << i))
            {
                LOG(INFO, "[PCA9685:%u] Set duty to %d", i, duty_[i]);
                write_pwm_duty(i, duty_[i]);
            }
        }
    }

    return NULL;
}

void Esp32PCA9685PWM::set_pwm_duty(unsigned channel, uint16_t counts)
{
    HASSERT(channel < NUM_CHANNELS);
    duty_[channel] = counts;

    {
        AtomicHolder h(this);
        dirty_ |= 0x1 << channel;
    }

    sem_.post();
}

void Esp32PCA9685PWM::write_pwm_duty(unsigned channel, uint16_t counts)
{
    uint8_t reg_base = LED_0_REG_ON + (channel * 4);
    if (counts > MAX_PWM_COUNTS)
    {
        LOG(INFO, "[PCA9685:%u] ON", channel);
        write_two_registers(reg_base, MAX_PWM_COUNTS, 0);
    }
    else if (counts == 0)
    {
        LOG(INFO, "[PCA9685:%u] OFF", channel);
        write_two_registers(reg_base, 0, MAX_PWM_COUNTS);
    }
    else
    {
        uint16_t on_counts = (channel * 256);
        uint16_t off_counts = (counts + (channel * 256)) % 0x1000;
        LOG(INFO, "[PCA9685:%u] on:%d, off:%d", channel, on_counts, off_counts);
        write_two_registers(reg_base, on_counts, off_counts);
    }
}

bool Esp32PCA9685PWM::read_register(uint8_t reg, uint8_t *value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress_ << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress_ << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
    i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t res = ESP_ERROR_CHECK_WITHOUT_ABORT(
        i2c_master_cmd_begin(I2C_PORT, cmd, MAX_I2C_WAIT_TICKS));
    i2c_cmd_link_delete(cmd);
    return res == ESP_OK;
}

bool Esp32PCA9685PWM::write_one_register(uint8_t reg, uint8_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress_ << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t res = ESP_ERROR_CHECK_WITHOUT_ABORT(
        i2c_master_cmd_begin(I2C_PORT, cmd, MAX_I2C_WAIT_TICKS));
    i2c_cmd_link_delete(cmd);
    return res == ESP_OK;
}

bool Esp32PCA9685PWM::write_one_register(uint8_t reg, uint16_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress_ << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, value & 0xFF, I2C_MASTER_ACK);
    i2c_master_write_byte(cmd, value >> 8, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t res = ESP_ERROR_CHECK_WITHOUT_ABORT(
        i2c_master_cmd_begin(I2C_PORT, cmd, MAX_I2C_WAIT_TICKS));
    i2c_cmd_link_delete(cmd);
    return res == ESP_OK;
}

bool Esp32PCA9685PWM::write_two_registers(uint8_t reg_base, uint16_t reg1_value
                                        , uint16_t reg2_value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2cAddress_ << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, reg_base, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, reg1_value & 0xFF, I2C_MASTER_ACK);
    i2c_master_write_byte(cmd, reg1_value >> 8, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, reg2_value & 0xFF, I2C_MASTER_ACK);
    i2c_master_write_byte(cmd, reg2_value >> 8, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t res = ESP_ERROR_CHECK_WITHOUT_ABORT(
        i2c_master_cmd_begin(I2C_PORT, cmd, MAX_I2C_WAIT_TICKS));
    i2c_cmd_link_delete(cmd);
    return res == ESP_OK;
}