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
 * \file web_server.cpp
 *
 * Built-in webserver for the ESP32 IO Board.
 *
 * @author Mike Dunston
 * @date 4 July 2020
 */

#include "sdkconfig.h"
#include "CDIClient.hxx"
#include "DelayRebootHelper.hxx"
#include "EventBroadcastHelper.hxx"
#include "nvs_config.hxx"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <freertos_drivers/esp32/Esp32WiFiManager.hxx>
#include <Httpd.h>
#include <HttpStringUtils.h>
#include <openlcb/MemoryConfigClient.hxx>
#include <os/MDNS.hxx>
#include <utils/constants.hxx>
#include <utils/FdUtils.hxx>
#include <utils/FileUtils.hxx>
#include <utils/logging.h>

static std::unique_ptr<http::Httpd> http_server;
static std::unique_ptr<CDIClient> cdi_client;
static openlcb::MemoryConfigClient *memory_client;
static MDNS mdns;
static uint64_t node_id;
static openlcb::NodeHandle node_handle;

/// Statically embedded index.html start location.
extern const uint8_t indexHtmlGz[] asm("_binary_index_html_gz_start");

/// Statically embedded index.html size.
extern const size_t indexHtmlGz_size asm("index_html_gz_length");

/// Statically embedded cash.js start location.
extern const uint8_t cashJsGz[] asm("_binary_cash_min_js_gz_start");

/// Statically embedded cash.js size.
extern const size_t cashJsGz_size asm("cash_min_js_gz_length");

/// Statically embedded spectre.min.css start location.
extern const uint8_t spectreMinCssGz[] asm("_binary_spectre_min_css_gz_start");

/// Statically embedded spectre.min.css size.
extern const size_t spectreMinCssGz_size asm("spectre_min_css_gz_length");

/// Cative portal landing page.
static constexpr const char * const CAPTIVE_PORTAL_HTML = R"!^!(
<html>
 <head>
  <title>%s v%s</title>
  <meta http-equiv="refresh" content="30;url='/captiveauth'" />
 </head>
 <body>
  <h1>Welcome to the %s configuration portal</h1>
  <h2>Navigate to any website and the %s configuration portal will be presented.</h2>
  <p>If this dialog does not automatically close, please click <a href="/captiveauth">here</a>.</p>
 </body>
</html>)!^!";

esp_ota_handle_t otaHandle;
esp_partition_t *ota_partition = nullptr;
HTTP_STREAM_HANDLER_IMPL(process_ota, request, filename, size, data, length, offset, final, abort_req)
{
    if (!offset)
    {
        ota_partition = (esp_partition_t *)esp_ota_get_next_update_partition(NULL);
        esp_err_t err = ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_ota_begin(ota_partition, size, &otaHandle));
        if (err != ESP_OK)
        {
            LOG_ERROR("[Web] OTA start failed, aborting!");
            request->set_status(http::HttpStatusCode::STATUS_SERVER_ERROR);
            *abort_req = true;
            return nullptr;
        }
        LOG(INFO, "[Web] OTA Update starting (%zu bytes, target:%s)...", size, ota_partition->label);
    }
    HASSERT(ota_partition);
    ESP_ERROR_CHECK(esp_ota_write(otaHandle, data, length));
    if (final)
    {
        esp_err_t err = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_end(otaHandle));
        if (err != ESP_OK)
        {
            LOG_ERROR("[Web] OTA end failed, aborting!");
            request->set_status(http::HttpStatusCode::STATUS_SERVER_ERROR);
            *abort_req = true;
            return nullptr;
        }
        LOG(INFO, "[Web] OTA binary received, setting boot partition: %s", ota_partition->label);
        err = ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_ota_set_boot_partition(ota_partition));
        if (err != ESP_OK)
        {
            LOG_ERROR("[Web] OTA end failed, aborting!");
            request->set_status(http::HttpStatusCode::STATUS_SERVER_ERROR);
            *abort_req = true;
            return nullptr;
        }
        LOG(INFO, "[Web] OTA Update Complete!");
        request->set_status(http::HttpStatusCode::STATUS_OK);
        Singleton<esp32io::DelayRebootHelper>::instance()->start();
        return new http::StringResponse("OTA Upload Successful, rebooting", http::MIME_TYPE_TEXT_PLAIN);
    }
    return nullptr;
}

// Helper which converts a string to a uint64 value.
uint64_t string_to_uint64(std::string value)
{
    // remove period characters if present
    value.erase(std::remove(value.begin(), value.end(), '.'), value.end());
    // convert the string to a uint64_t value
    return std::stoull(value, nullptr, 16);
}

namespace esp32io
{
void factory_reset_events();
} // namespace esp32io

static uint32_t WS_REQ_ID = 0;

WEBSOCKET_STREAM_HANDLER_IMPL(websocket_proc, socket, event, data, len)
{
    if (event == http::WebSocketEvent::WS_EVENT_TEXT)
    {
        string response = R"!^!({"res":"error","error":"Request not understood"})!^!";
        string req = string((char *)data, len);
        cJSON *root = cJSON_Parse(req.c_str());
        cJSON *req_type = cJSON_GetObjectItem(root, "req");
        if (req_type == NULL)
        {
            // NO OP, the websocket is outbound only to trigger events on the client side.
            LOG(INFO, "[Web] Failed to parse:%s", req.c_str());
        }
        else if (!strcmp(req_type->valuestring, "set-nodeid"))
        {
            std::string value =
                cJSON_GetObjectItem(root, "value")->valuestring;
            uint64_t new_node_id = string_to_uint64(value);
            if (set_node_id(new_node_id))
            {
                LOG(INFO, "[Web] Node ID updated to: %s, reboot pending"
                , uint64_to_string_hex(new_node_id).c_str());
                Singleton<esp32io::DelayRebootHelper>::instance()->start();
                response = R"!^!({"res":"set-nodeid"})!^!";
            }
            else
            {
                response = R"!^!({"res":"error","error":"Failed to update node-id"})!^!";
            }
        }
        else if (!strcmp(req_type->valuestring, "info"))
        {
            const esp_app_desc_t *app_data = esp_ota_get_app_description();
            const esp_partition_t *partition = esp_ota_get_running_partition();
            response =
                StringPrintf(R"!^!({"res":"info","build":"%s","timestamp":"%s %s","ota":"%s","snip_name":"%s","snip_hw":"%s","snip_sw":"%s","node_id":"%s"})!^!",
                    app_data->version, app_data->date
                  , app_data->time, partition->label
                  , openlcb::SNIP_STATIC_DATA.model_name
                  , openlcb::SNIP_STATIC_DATA.hardware_version
                  , openlcb::SNIP_STATIC_DATA.software_version
                  , uint64_to_string_hex(node_id).c_str());
        }
        else if (!strcmp(req_type->valuestring, "cdi"))
        {
            if (!cJSON_HasObjectItem(root, "ofs") ||
                !cJSON_HasObjectItem(root, "type") ||
                !cJSON_HasObjectItem(root, "sz") ||
                !cJSON_HasObjectItem(root, "tgt"))
            {
                LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s"
                    , WS_REQ_ID, req.c_str());
                response =
                StringPrintf(
                R"!^!({"res":"error", "error":"request is missing one (or more) required parameters","id":%d})!^!"
                , WS_REQ_ID);
            }
            else
            {
                size_t offs = cJSON_GetObjectItem(root, "ofs")->valueint;
                std::string param_type =
                    cJSON_GetObjectItem(root, "type")->valuestring;
                size_t size = cJSON_GetObjectItem(root, "sz")->valueint;
                string target = cJSON_GetObjectItem(root, "tgt")->valuestring;
                BufferPtr<CDIClientRequest> b(cdi_client->alloc());

                if (!cJSON_HasObjectItem(root, "val"))
                {
                    LOG(VERBOSE,
                        "[WSJSON:%d] Sending CDI READ: offs:%zu size:%zu type:%s tgt:%s",
                        WS_REQ_ID, offs, size, param_type.c_str(),
                        target.c_str());
                    b->data()->reset(CDIClientRequest::READ, node_handle,
                                     socket, WS_REQ_ID++, offs, size, target,
                                     param_type);
                }
                else
                {
                    string value = "";
                    cJSON *raw_value = cJSON_GetObjectItem(root, "val");
                    if (param_type == "str")
                    {
                        // copy of up to the reported size.
                        value = string(raw_value->valuestring, size);
                        // ensure value is null terminated
                        value += '\0';
                    }
                    else if (param_type == "int")
                    {
                        if (size == 1)
                        {
                            uint8_t data8 = std::stoi(raw_value->valuestring);
                            value.clear();
                            value.push_back(data8);
                        }
                        else if (size == 2)
                        {
                            uint16_t data16 = std::stoi(raw_value->valuestring);
                            value.clear();
                            value.push_back((data16 >> 8) & 0xFF);
                            value.push_back(data16 & 0xFF);
                        }
                        else
                        {
                            uint32_t data32 = std::stoul(raw_value->valuestring);
                            value.clear();
                            value.push_back((data32 >> 24) & 0xFF);
                            value.push_back((data32 >> 16) & 0xFF);
                            value.push_back((data32 >> 8) & 0xFF);
                            value.push_back(data32 & 0xFF);
                        }
                    }
                    else if (param_type == "evt")
                    {
                        uint64_t data =
                            string_to_uint64(string(raw_value->valuestring));
                        value.clear();
                        value.push_back((data >> 56) & 0xFF);
                        value.push_back((data >> 48) & 0xFF);
                        value.push_back((data >> 40) & 0xFF);
                        value.push_back((data >> 32) & 0xFF);
                        value.push_back((data >> 24) & 0xFF);
                        value.push_back((data >> 16) & 0xFF);
                        value.push_back((data >> 8) & 0xFF);
                        value.push_back(data & 0xFF);
                    }
                    LOG(VERBOSE,
                        "[WSJSON:%d] Sending CDI WRITE: offs:%zu value:%s tgt:%s",
                        WS_REQ_ID, offs, raw_value->valuestring,
                        target.c_str());
                    b->data()->reset(CDIClientRequest::WRITE, node_handle,
                                     socket, WS_REQ_ID++, offs, size, target,
                                     value);
                }
                b->data()->done.reset(EmptyNotifiable::DefaultInstance());
                cdi_client->send(b->ref());
                cJSON_Delete(root);
                return;
            }
        }
        else if (!strcmp(req_type->valuestring, "update-complete"))
        {
            LOG(VERBOSE, "[WSJSON:%d] Sending UPDATE_COMPLETE to queue",
                WS_REQ_ID);
            BufferPtr<CDIClientRequest> b(cdi_client->alloc());
            b->data()->reset(CDIClientRequest::UPDATE_COMPLETE,
                             node_handle, socket, WS_REQ_ID++);
            b->data()->done.reset(EmptyNotifiable::DefaultInstance());
            cdi_client->send(b->ref());
            cJSON_Delete(root);
            return;
        }
        else if (!strcmp(req_type->valuestring, "factory-reset"))
        {
            if (force_factory_reset())
            {
                Singleton<esp32io::DelayRebootHelper>::instance()->start();
                response = R"!^!({"res":"factory-reset"})!^!";
            }
        }
        else if (!strcmp(req_type->valuestring, "reset-events"))
        {
            esp32io::factory_reset_events();
            response = R"!^!({"res":"reset-events"})!^!";
        }
        else if (!strcmp(req_type->valuestring, "event-test"))
        {
            string value = cJSON_GetObjectItem(root, "value")->valuestring;
            uint64_t eventID = string_to_uint64(value);
            Singleton<esp32io::EventBroadcastHelper>::instance()->send_event(eventID);
            response = R"!^!({"res":"event"})!^!";
        }
        else
        {
            LOG_ERROR("Unrecognized request: %s", req.c_str());
        }
        cJSON_Delete(root);
        LOG(VERBOSE, "[Web] WS: %s -> %s", req.c_str(), response.c_str());
        response += "\n";
        socket->send_text(response);
    }
}

HTTP_HANDLER_IMPL(fs_proc, request)
{
    string path = request->param("path");
    LOG(VERBOSE, "[Web] Searching for path: %s", path.c_str());
    struct stat statbuf;
    // verify that the requested path exists
    if (!stat(path.c_str(), &statbuf))
    {
        string data = read_file_to_string(path);
        string mimetype = http::MIME_TYPE_TEXT_PLAIN;
        if (path.find(".xml") != string::npos)
        {
            mimetype = http::MIME_TYPE_TEXT_XML;
            // CDI xml files have a trailing null, this can cause
            // issues in browsers parsing/rendering the XML data.
            if (request->param("remove_nulls", false))
            {
                std::replace(data.begin(), data.end(), '\0', ' ');
            }
        }
        else if (path.find(".json") != string::npos)
        {
            mimetype = http::MIME_TYPE_APPLICATION_JSON;
        }
        return new http::StringResponse(data, mimetype);
    }
    LOG(INFO, "[Web] Path not found");
    request->set_status(http::HttpStatusCode::STATUS_NOT_FOUND);
    return nullptr;
}

void init_webserver(openlcb::MemoryConfigClient *cfg_client,
                    openmrn_arduino::Esp32WiFiManager *wifi_mgr, uint64_t id)
{
    const esp_app_desc_t *app_data = esp_ota_get_app_description();
    memory_client = cfg_client;
    node_id = id;
    node_handle = openlcb::NodeHandle(id);
    LOG(INFO, "[Httpd] Initializing webserver");
    http_server.reset(new http::Httpd(wifi_mgr, &mdns));
    http_server->redirect_uri("/", "/index.html");
    http_server->static_uri("/index.html", indexHtmlGz, indexHtmlGz_size
                          , http::MIME_TYPE_TEXT_HTML
                          , http::HTTP_ENCODING_GZIP
                          , false);
    http_server->static_uri("/cash.min.js", cashJsGz, cashJsGz_size
                          , http::MIME_TYPE_TEXT_JAVASCRIPT
                          , http::HTTP_ENCODING_GZIP);
    http_server->static_uri("/spectre.min.css", spectreMinCssGz
                          , spectreMinCssGz_size, http::MIME_TYPE_TEXT_CSS
                          , http::HTTP_ENCODING_GZIP);
    http_server->websocket_uri("/ws", websocket_proc);
    http_server->uri("/fs", http::HttpMethod::GET, fs_proc);
    http_server->uri("/ota", http::HttpMethod::POST, nullptr, process_ota);
    http_server->captive_portal(
        StringPrintf(CAPTIVE_PORTAL_HTML, app_data->project_name
                    , app_data->version, app_data->project_name
                    , app_data->project_name));
}

void shutdown_webserver()
{
    LOG(INFO, "[Httpd] Shutting down webserver");
    http_server.reset(nullptr);
}