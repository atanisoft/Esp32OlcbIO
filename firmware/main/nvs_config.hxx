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
 * \file nvs_config.hxx
 *
 * NVS based configuration management for the ESP32 IO Board.
 *
 * @author Mike Dunston
 * @date 4 July 2020
 */

#ifndef NVS_CONFIG_HXX_
#define NVS_CONFIG_HXX_

#include <esp_err.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdint.h>
#include <string>

static constexpr size_t AP_SSID_PASS_LEN = 65;
static constexpr size_t HOSTNAME_PREFIX_LEN = 21;
typedef struct
{
    bool force_reset;
    uint64_t node_id;
    wifi_mode_t wifi_mode;
    char hostname_prefix[HOSTNAME_PREFIX_LEN];
    char sta_ssid[AP_SSID_PASS_LEN];
    char sta_pass[AP_SSID_PASS_LEN];
    bool sta_wait_for_connect;
    uint32_t sta_ip;
    uint32_t sta_gw;
    uint32_t sta_nm;
    wifi_auth_mode_t ap_auth;
    char ap_ssid[AP_SSID_PASS_LEN];
    char ap_pass[AP_SSID_PASS_LEN];
    uint8_t ap_channel;
    uint8_t reserved[9];
} node_config_t;

esp_err_t load_config(node_config_t *config);
esp_err_t save_config(node_config_t *config);
esp_err_t default_config(node_config_t *config);
void nvs_init();
void dump_config(node_config_t *config);
bool reconfigure_wifi(wifi_mode_t, const std::string &ssid, const std::string &password);
bool force_factory_reset();
bool reset_wifi_config_to_softap(node_config_t *config);

#endif // NVS_CONFIG_HXX_