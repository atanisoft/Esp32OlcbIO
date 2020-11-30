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
 * \file esp32io_bootloader.cpp
 *
 * Firmware download bootloader support for the ESP32 IO Board.
 *
 * @author Mike Dunston
 * @date 30 November 2020
 */

#include "sdkconfig.h"
#include "hardware.hxx"
#include "nvs_config.hxx"

#define BOOTLOADER_STREAM
#define WRITE_BUFFER_SIZE (CONFIG_WL_SECTOR_SIZE / 2)
#include <bootloader_hal.h>
#include <driver/twai.h>
#include <esp_ota_ops.h>
#include <openlcb/Bootloader.hxx>
#include <utils/Hub.hxx>
#include <utils/constants.hxx>
#include <utils/Uninitialized.hxx>

struct app_header current_app_header;
node_config_t *node_cfg;
esp_partition_t *current_partition;
esp_partition_t *target_partition;
esp_ota_handle_t ota_handle;

static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static twai_general_config_t g_config =
    TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_TWAI_TX_PIN, CONFIG_TWAI_RX_PIN,
                                TWAI_MODE_NORMAL);

extern "C"
{

void bootloader_hw_set_to_safe(void)
{
    LOG(VERBOSE, "[Bootloader] bootloader_hw_set_to_safe");
    LED_WIFI_Pin::hw_init();
    LED_ACTIVITY_Pin::hw_init();
    g_config.tx_queue_len = config_can_tx_buffer_size();
    g_config.rx_queue_len = config_can_rx_buffer_size();
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
}

void bootloader_hw_init(void)
{
    LOG(VERBOSE, "[Bootloader] bootloader_hw_init");
}

bool request_bootloader(void)
{
    LOG(VERBOSE, "[Bootloader] request_bootloader");
    return true;
}

void application_entry(void)
{
    LOG(VERBOSE, "[Bootloader] application_entry");
}

void bootloader_reboot(void)
{
    LOG(INFO, "[Bootloader] Rebooting the node");
}

void bootloader_led(enum BootloaderLed led, bool value)
{
    LOG(VERBOSE, "[Bootloader] bootloader_led(%d, %d)", led, value);
    if (led == LED_ACTIVE)
    {
        LED_ACTIVITY_Pin::set(value);
    }
    else if (led == LED_WRITING)
    {
        LED_WIFI_Pin::set(value);
    }
}

bool read_can_frame(struct can_frame *frame)
{
    twai_message_t rx_msg;
    bzero(&rx_msg, sizeof(twai_message_t));
    if (twai_receive(&rx_msg, pdMS_TO_TICKS(250)) == ESP_OK)
    {
        LOG(VERBOSE, "[Bootloader] CAN_RX");
        frame->can_id = rx_msg.identifier;
        frame->can_dlc = rx_msg.data_length_code;
        frame->can_err = 0;
        frame->can_eff = rx_msg.extd;
        frame->can_rtr = rx_msg.rtr;
        memcpy(frame->data, rx_msg.data, frame->can_dlc);
        return true;
    }
    return false;
}

bool try_send_can_frame(const struct can_frame &frame)
{
    twai_message_t tx_msg;
    bzero(&tx_msg, sizeof(twai_message_t));
    tx_msg.identifier = frame.can_id;
    tx_msg.data_length_code = frame.can_dlc;
    tx_msg.extd = frame.can_eff;
    tx_msg.rtr = frame.can_rtr;
    memcpy(tx_msg.data, frame.data, frame.can_dlc);
    if (twai_transmit(&tx_msg, pdMS_TO_TICKS(250)) == ESP_OK)
    {
        LOG(VERBOSE, "[Bootloader] CAN_TX");
        return true;
    }
    return false;
}

void get_flash_boundaries(const void **flash_min, const void **flash_max,
    const struct app_header **app_header)
{
    LOG(VERBOSE, "[Bootloader] get_flash_boundaries(%d,%d)"
      , 0, current_app_header.app_size);
    *((uint32_t *)flash_min) = 0;
    *((uint32_t *)flash_max) = current_app_header.app_size;
    *app_header = &current_app_header;
}

void get_flash_page_info(
    const void *address, const void **page_start, uint32_t *page_length_bytes)
{
    uint32_t value = (uint32_t)address;
    value &= ~(CONFIG_WL_SECTOR_SIZE - 1);
    *page_start = (const void *)value;
    *page_length_bytes = CONFIG_WL_SECTOR_SIZE;
    LOG(VERBOSE, "[Bootloader] get_flash_page_info(%d, %d)", value
      , *page_length_bytes);
}

void erase_flash_page(const void *address)
{
    // NO OP -- handled automatically as part of write.
    uint32_t addr = (uint32_t)address;
    LOG(VERBOSE, "[Bootloader] Erase: %d", addr);
}

void write_flash(const void *address, const void *data, uint32_t size_bytes)
{
    uint32_t addr = (uint32_t)address;
    LOG(VERBOSE, "[Bootloader] Write: %d, %d", addr, size_bytes);
    bootloader_led(LED_WRITING, true);
    bootloader_led(LED_ACTIVE, false);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_write(ota_handle, data, size_bytes));
    bootloader_led(LED_WRITING, false);
    bootloader_led(LED_ACTIVE, true);
}

uint16_t flash_complete(void)
{
    LOG(INFO, "[Bootloader] Finalizing OTA");
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_end(ota_handle));
    LOG(INFO, "[Bootloader] Updating next boot partition");
    ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_ota_set_boot_partition(target_partition));
    return 0;
}

void checksum_data(const void* data, uint32_t size, uint32_t* checksum)
{
    LOG(VERBOSE, "[Bootloader] checksum_data");
    memset(checksum, 0, 16);
}

uint16_t nmranet_alias(void)
{
    LOG(VERBOSE, "[Bootloader] nmranet_alias");
    // let the bootloader generate it based on nmranet_nodeid().
    return 0;
}

uint64_t nmranet_nodeid(void)
{
    LOG(VERBOSE, "[Bootloader] nmranet_nodeid");
    return node_cfg->node_id;
}

} // extern "C"

void start_bootloader_stack(node_config_t *config)
{
    bzero(&current_app_header, sizeof(struct app_header));
    current_partition = (esp_partition_t *)esp_ota_get_running_partition();
    current_app_header.app_size = current_partition->size;
    node_cfg = config;
    target_partition =
        (esp_partition_t *)esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_ota_begin(target_partition, OTA_SIZE_UNKNOWN, &ota_handle));
    LOG(VERBOSE, "[Bootloader] calling bootloader_entry");
    bootloader_entry();
    LOG(INFO, "[Reboot] Restarting!");
    esp_restart();
}