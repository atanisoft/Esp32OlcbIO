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
 * \file nvs_config.cpp
 *
 * NVS based configuration management for the ESP32 IO Board.
 *
 * @author Mike Dunston
 * @date 4 July 2020
 */

#include "hardware.hxx"
#include "nvs_config.hxx"
#include "sdkconfig.h"

// TODO: adjust format_utils.hxx not to require this line here.
using std::string;

#include <utils/format_utils.hxx>
#include <utils/logging.h>

/// NVS Persistence namespace.
static constexpr char NVS_NAMESPACE[] = "iocfg";

/// NVS Persistence key.
static constexpr char NVS_CFG_KEY[] = "cfg";

esp_err_t load_config(node_config_t *config)
{
    LOG(INFO, "[NVS] Loading configuration");
    // load non-CDI based config from NVS
    nvs_handle_t nvs;
    size_t size = sizeof(node_config_t);
    esp_err_t res =
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));
    if (res != ESP_OK)
    {
        LOG_ERROR("[NVS] Configuration load failed: %s (%d)"
                , esp_err_to_name(res), res);
        return res;
    }
    res = nvs_get_blob(nvs, NVS_CFG_KEY, config, &size);
    nvs_close(nvs);

    // if the size read in is not as expected reset the result code to failure.
    if (size != sizeof(node_config_t))
    {
        LOG_ERROR("[NVS] Configuration load failed (loaded size incorrect: "
                  "%zu vs %zu)", size, sizeof(node_config_t));
        res = ESP_FAIL;
    }
    if (config->wifi_mode != WIFI_MODE_NULL)
    {
        if (config->wifi_mode != WIFI_MODE_STA && config->ap_ssid[0] == '\0')
        {
            LOG_ERROR("[NVS] Configuration doesn't appear to be valid, AP "
                      "SSID is blank!");
            res = ESP_FAIL;
        }
        if (config->wifi_mode != WIFI_MODE_AP && config->sta_ssid[0] == '\0')
        {
            LOG_ERROR("[NVS] Configuration doesn't appear to be valid, "
                      "Station SSID is blank!");
            res = ESP_FAIL;
        }
    }

    if (config->ap_channel == 0)
    {
        config->ap_channel = 1;
    }
    return res;
}

esp_err_t save_config(node_config_t *config)
{
    nvs_handle_t nvs;
    esp_err_t res =
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));
    if (res != ESP_OK)
    {
        LOG_ERROR("[NVS] Configuration save failed: %s (%d)"
                , esp_err_to_name(res), res);
        return res;
    }
    res =
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            nvs_set_blob(nvs, NVS_CFG_KEY, config, sizeof(node_config_t)));
    if (res != ESP_OK)
    {
        LOG_ERROR("[NVS] Configuration save failed: %s (%d)"
                , esp_err_to_name(res), res);
        return res;
    }
    res = ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvs));
    nvs_close(nvs);
    if (res != ESP_OK)
    {
        LOG_ERROR("[NVS] Commit failed: %s (%d)", esp_err_to_name(res), res);
    }
    return res;
}


#ifndef CONFIG_WIFI_STATION_SSID
#define CONFIG_WIFI_STATION_SSID ""
#endif

#ifndef CONFIG_WIFI_STATION_PASSWORD
#define CONFIG_WIFI_STATION_PASSWORD ""
#endif

#ifndef CONFIG_WIFI_SOFTAP_SSID
#define CONFIG_WIFI_SOFTAP_SSID "esp32io"
#endif

#ifndef CONFIG_WIFI_SOFTAP_PASSWORD
#define CONFIG_WIFI_SOFTAP_PASSWORD "esp32io"
#endif

#ifndef CONFIG_WIFI_RESTART_ON_SSID_CONNECT_FAILURE
#define CONFIG_WIFI_RESTART_ON_SSID_CONNECT_FAILURE 0
#endif

#ifndef CONFIG_WIFI_HOSTNAME_PREFIX
#define CONFIG_WIFI_HOSTNAME_PREFIX "esp32io_"
#endif

#ifndef CONFIG_WIFI_SOFTAP_CHANNEL
#define CONFIG_WIFI_SOFTAP_CHANNEL 1
#endif

esp_err_t default_config(node_config_t *config)
{
    LOG(INFO, "[NVS] Initializing default configuration");
    bzero(config, sizeof(node_config_t));
    config->node_id = CONFIG_OLCB_NODE_ID;
    config->wifi_mode = (wifi_mode_t)CONFIG_WIFI_MODE;
    strcpy(config->sta_ssid, CONFIG_WIFI_STATION_SSID);
    strcpy(config->sta_pass, CONFIG_WIFI_STATION_PASSWORD);
    config->sta_wait_for_connect = CONFIG_WIFI_RESTART_ON_SSID_CONNECT_FAILURE;
    config->ap_channel = CONFIG_WIFI_SOFTAP_CHANNEL;
    strcpy(config->ap_ssid, CONFIG_WIFI_SOFTAP_SSID);
    strcpy(config->ap_pass, CONFIG_WIFI_SOFTAP_PASSWORD);
    strcpy(config->hostname_prefix, CONFIG_WIFI_HOSTNAME_PREFIX);
    config->ap_auth = WIFI_AUTH_WPA2_PSK;
    return save_config(config);
}

void die_with(bool wifi, bool activity, unsigned period = 1000
            , bool toggle_both = false);

void nvs_init()
{
    // Initialize NVS before we do any other initialization as it may be
    // internally used by various components even if we disable it's usage in
    // the WiFi connection stack.
    LOG(INFO, "[NVS] Initializing NVS");
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_init()) == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        const esp_partition_t *part =
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA
                                   , ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
        if (part != NULL)
        {
            LOG(INFO, "[NVS] Erasing partition %s...", part->label);
            ESP_ERROR_CHECK(esp_partition_erase_range(part, 0, part->size));
            ESP_ERROR_CHECK(nvs_flash_init());
        }
        else
        {
            LOG_ERROR("[NVS] Unable to locate NVS partition!");
            die_with(true, false);
        }
    }
}

void dump_config(node_config_t *config)
{
    uint8_t mac[6];
    switch(config->wifi_mode)
    {
        case WIFI_MODE_STA:
            bzero(&mac, 6);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_read_mac(mac, ESP_MAC_WIFI_STA));
            LOG(INFO, "[NVS] WiFi mode: %d (Station:%s)", config->wifi_mode
              , config->sta_ssid);
            LOG(INFO, "[NVS] Station MAC: %s", mac_to_string(mac).c_str());
            break;
        case WIFI_MODE_AP:
            bzero(&mac, 6);
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
            LOG(INFO, "[NVS] WiFi mode: %d (SoftAP:%s, auth:%d, channel:%d)"
              , config->wifi_mode, config->ap_ssid, config->ap_auth
              , config->ap_channel);
            LOG(INFO, "[NVS] SoftAP MAC: %s", mac_to_string(mac).c_str());
            //LOG(INFO, "[NVS] SoftAP PW: %s", config->ap_pass);
            break;
        case WIFI_MODE_APSTA:
            bzero(&mac, 6);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_read_mac(mac, ESP_MAC_WIFI_STA));
            LOG(INFO, "[NVS] WiFi mode: %d (Station:%s, SoftAP:esp32s2io_%s)"
              , config->wifi_mode, config->ap_ssid
              , uint64_to_string_hex(config->node_id).c_str());
            LOG(INFO, "[NVS] Station MAC: %s", mac_to_string(mac).c_str());
            bzero(&mac, 6);
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
            LOG(INFO, "[NVS] SoftAP MAC: %s", mac_to_string(mac).c_str());
            break;
        case WIFI_MODE_NULL:
        case WIFI_MODE_MAX:
            LOG(INFO, "[NVS] WiFi mode: %d (OFF)", config->wifi_mode);
    }
}

bool reconfigure_wifi(wifi_mode_t mode, const string &ssid
                    , const string &password)
{
    if (ssid.length() > AP_SSID_PASS_LEN)
    {
        LOG_ERROR("[NVS] Requested SSID is longer than permitted: %zu (max:%d)"
                , ssid.length(), AP_SSID_PASS_LEN);
        return false;
    }
    if (password.length() > AP_SSID_PASS_LEN)
    {
        LOG_ERROR("[NVS] Requested PASSWORD is longer than permitted: %zu "
                  "(max:%d)", password.length(), AP_SSID_PASS_LEN);
        return false;
    }

    node_config_t config;
    load_config(&config);
    LOG(INFO, "[NVS] Setting wifi_mode to: %d (%s)", mode
      , mode == WIFI_MODE_NULL ? "Off" :
        mode == WIFI_MODE_STA ? "Station" :
        mode == WIFI_MODE_APSTA ? "Station + SoftAP" : "SoftAP");
    config.wifi_mode = mode;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
    {
        LOG(INFO, "[NVS] Setting STATION ssid to: %s", ssid.c_str());
        strcpy(config.sta_ssid, ssid.c_str());
        strcpy(config.sta_pass, password.c_str());
    }
    else if (mode == WIFI_MODE_AP)
    {
        LOG(INFO, "[NVS] Setting AP ssid to: %s", ssid.c_str());
        strcpy(config.ap_ssid, ssid.c_str());
        strcpy(config.ap_pass, password.c_str());
    }
    return save_config(&config) == ESP_OK;
}

bool force_factory_reset()
{
    node_config_t config;
    load_config(&config);
    config.force_reset = true;

    return save_config(&config) == ESP_OK;
}

bool reset_wifi_config_to_softap(node_config_t *config)
{
    LOG(WARNING, "[NVS] Switching to SoftAP mode as the station SSID is blank!");
    config->wifi_mode = WIFI_MODE_AP;
    if (strlen(config->ap_ssid) == 0)
    {
        LOG(WARNING, "[NVS] SoftAP SSID is blank, resetting to %s"
          , CONFIG_WIFI_SOFTAP_SSID);
        strcpy(config->ap_ssid, CONFIG_WIFI_SOFTAP_SSID);
        strcpy(config->ap_pass, CONFIG_WIFI_SOFTAP_PASSWORD);
    }
    return save_config(config) == ESP_OK;
}