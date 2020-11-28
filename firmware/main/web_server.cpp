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


#include "DelayRebootHelper.hxx"
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
static std::unique_ptr<openlcb::MemoryConfigClient> memory_client;
static MDNS mdns;
static node_config_t *node_cfg;
static openlcb::SimpleStackBase *node_stack;
static Executor<1> http_executor{NO_THREAD()};

/// Statically embedded index.html start location.
extern const uint8_t indexHtmlGz[] asm("_binary_index_html_gz_start");

/// Statically embedded index.html size.
extern const size_t indexHtmlGz_size asm("index_html_gz_length");

/// Statically embedded cash.js start location.
extern const uint8_t cashJsGz[] asm("_binary_cash_min_js_gz_start");

/// Statically embedded cash.js size.
extern const size_t cashJsGz_size asm("cash_min_js_gz_length");

/// Statically embedded milligram.min.css start location.
extern const uint8_t milligramMinCssGz[] asm("_binary_milligram_min_css_gz_start");

/// Statically embedded milligram.min.css size.
extern const size_t milligramMinCssGz_size asm("milligram_min_css_gz_length");

/// Statically embedded normalize.min.css start location.
extern const uint8_t normalizeMinCssGz[] asm("_binary_normalize_min_css_gz_start");

/// Statically embedded normalize.min.css size.
extern const size_t normalizeMinCssGz_size asm("normalize_min_css_gz_length");

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

#if CONFIG_EXTERNAL_HTTP_EXECUTOR
static void http_exec_task(void *param)
{
    LOG(INFO, "[Httpd] Executor starting...");
    http_executor.thread_body();
    LOG(INFO, "[Httpd] Executor stopped...");
    vTaskDelete(nullptr);
}
#endif // CONFIG_EXTERNAL_HTTP_EXECUTOR

using openlcb::MemoryConfigClientRequest;
using openlcb::MemoryConfigDefs;
using openlcb::NodeHandle;

void factory_reset_events();

class CDIRequestProcessor : public StateFlowBase
{
public:
    CDIRequestProcessor(http::WebSocketFlow *socket, size_t offs, size_t size
                      , string target, string type, string value = "")
                      : StateFlowBase(node_stack->service()), socket_(socket)
                      , offs_(offs), size_(size), target_(target), type_(type)
                      , value_(value)
                      , nodeHandle_(openlcb::NodeID(CONFIG_OLCB_NODE_ID))
    {
        start_flow(STATE(send_request));
    }

private:
    uint8_t attempts_{3};
    http::WebSocketFlow *socket_;
    size_t offs_;
    size_t size_;
    string target_;
    string type_;
    string value_;
    NodeHandle nodeHandle_;

    Action send_request()
    {
        if (!value_.empty())
        {
            LOG(VERBOSE, "[CDI:%s] Writing %zu bytes from offset %zu"
              , target_.c_str(), size_, offs_);
            return invoke_subflow_and_wait(memory_client.get()
                                         , STATE(response_received)
                                         , MemoryConfigClientRequest::WRITE
                                         , nodeHandle_
                                         , MemoryConfigDefs::SPACE_CONFIG
                                         , offs_, value_);
        }
        LOG(VERBOSE, "[CDI:%s] Requesting %zu bytes from offset %zu"
          , target_.c_str(), size_, offs_);
        return invoke_subflow_and_wait(memory_client.get()
                                     , STATE(response_received)
                                     , MemoryConfigClientRequest::READ_PART
                                     , nodeHandle_
                                     , MemoryConfigDefs::SPACE_CONFIG
                                     , offs_, size_);
    }

    Action response_received()
    {
        auto b = get_buffer_deleter(full_allocation_result(memory_client.get()));
        string response;
        if (b->data()->resultCode)
        {
            --attempts_;
            if (attempts_ > 0)
            {
                LOG_ERROR("[CDI:%s] Failed to execute request: %d (%d "
                          "attempts remaining)"
                        , target_.c_str(), b->data()->resultCode, attempts_);
                return yield_and_call(STATE(send_request));
            }
            response =
                StringPrintf(
                    R"!^!({"resp_type":"error","error":"Request failed: %d"})!^!"
                  , b->data()->resultCode);
        }
        else if (value_.empty())
        {
            LOG(VERBOSE, "[CDI:%s] Received %zu bytes from offset %zu"
              , target_.c_str(), size_, offs_);
            if (type_ == "string")
            {
                response =
                    StringPrintf(
                        R"!^!({"resp_type":"field-value","target":"%s","value":"%s","type":"string"})!^!"
                      , target_.c_str(), b->data()->payload.c_str());
            }
            else if (type_ == "int")
            {
                uint32_t data = b->data()->payload.data()[0];
                if (size_ == 2)
                {
                    uint16_t data16 = 0;
                    memcpy(&data16, b->data()->payload.data(), sizeof(uint16_t));
                    data = be16toh(data16);
                }
                else if (size_ == 4)
                {
                    uint32_t data32 = 0;
                    memcpy(&data32, b->data()->payload.data(), sizeof(uint32_t));
                    data = be32toh(data32);
                }
                response =
                    StringPrintf(
                        R"!^!({"resp_type":"field-value","target":"%s","value":"%d","type":"int"})!^!"
                    , target_.c_str(), data);
            }
            else if (type_ == "eventid")
            {
                uint64_t event_id = 0;
                memcpy(&event_id, b->data()->payload.data(), sizeof(uint64_t));
                response =
                    StringPrintf(
                        R"!^!({"resp_type":"field-value","target":"%s","value":"%s","type":"eventid"})!^!"
                    , target_.c_str()
                    , uint64_to_string_hex(be64toh(event_id)).c_str());
            }
        }
        else
        {
            response =
                StringPrintf(R"!^!({"resp_type":"field-saved","target":"%s"})!^!"
                           , target_.c_str());
        }
        response += "\n";
        socket_->send_text(response);

        return yield_and_call(STATE(cleanup_request));
    }

    Action cleanup_request()
    {
        return delete_this();
    }
};

WEBSOCKET_STREAM_HANDLER_IMPL(websocket_proc, socket, event, data, len)
{
    if (event == http::WebSocketEvent::WS_EVENT_TEXT)
    {
        string response = R"!^!({"resp_type":"error","error":"Request not understood"})!^!";
        string req = string((char *)data, len);
        cJSON *root = cJSON_Parse(req.c_str());
        cJSON *req_type = cJSON_GetObjectItem(root, "req_type");
        if (req_type == NULL)
        {
            // NO OP, the websocket is outbound only to trigger events on the client side.
            LOG(INFO, "[Web] Failed to parse:%s", req.c_str());
        }
        else if (!strcmp(req_type->valuestring, "info"))
        {
            const esp_app_desc_t *app_data = esp_ota_get_app_description();
            const esp_partition_t *partition = esp_ota_get_running_partition();
            response =
                StringPrintf(R"!^!({"resp_type":"info","build":"%s","timestamp":"%s %s","ota":"%s","snip_name":"%s","snip_hw":"%s","snip_sw":"%s","node_id":"%s"})!^!",
                                app_data->version, app_data->date,
                                app_data->time, partition->label,
                                openlcb::SNIP_STATIC_DATA.model_name,
                                openlcb::SNIP_STATIC_DATA.hardware_version,
                                openlcb::SNIP_STATIC_DATA.software_version,
                                uint64_to_string_hex(CONFIG_OLCB_NODE_ID).c_str());
        }
        else if (!strcmp(req_type->valuestring, "wifi"))
        {
            response =
                StringPrintf(R"!^!({"resp_type":"wifi","mode":%d,"sta_ssid":"%s","ap_ssid":"%s"})!^!"
                            , node_cfg->wifi_mode, node_cfg->sta_ssid, node_cfg->ap_ssid);
        }
        else if (!strcmp(req_type->valuestring, "cdi-get"))
        {
            new CDIRequestProcessor(socket, cJSON_GetObjectItem(root, "offs")->valueint
                                    , cJSON_GetObjectItem(root, "size")->valueint
                                    , cJSON_GetObjectItem(root, "target")->valuestring
                                    , cJSON_GetObjectItem(root, "type")->valuestring);
            cJSON_Delete(root);
            return;
        }
        else if (!strcmp(req_type->valuestring, "update-complete"))
        {
            auto b = invoke_flow(memory_client.get()
                                , MemoryConfigClientRequest::UPDATE_COMPLETE
                                , openlcb::NodeHandle(openlcb::NodeID(CONFIG_OLCB_NODE_ID)));
            response =
                StringPrintf(R"!^!({"resp_type":"update-complete","code":%d})!^!"
                            , b->data()->resultCode);
        }
        else if (!strcmp(req_type->valuestring, "cdi-set"))
        {
            size_t offs = cJSON_GetObjectItem(root, "offs")->valueint;
            std::string param_type = cJSON_GetObjectItem(root, "type")->valuestring;
            size_t size = cJSON_GetObjectItem(root, "size")->valueint;
            string value = cJSON_GetObjectItem(root, "value")->valuestring;
            string target = cJSON_GetObjectItem(root, "target")->valuestring;
            if (param_type == "string")
            {
                // make sure value is null terminated
                value += '\0';
                new CDIRequestProcessor(socket, offs, size, target, param_type, value);
            }
            else if (param_type == "int")
            {
                if (size == 1)
                {
                    uint8_t data8 = std::stoi(value);
                    value.clear();
                    value.push_back(data8);
                    new CDIRequestProcessor(socket, offs, size, target, param_type, value);
                }
                else if (size == 2)
                {
                    uint16_t data16 = std::stoi(value);
                    value.clear();
                    value.push_back((data16 >> 8) & 0xFF);
                    value.push_back(data16 & 0xFF);
                    new CDIRequestProcessor(socket, offs, size, target, param_type, value);
                }
                else
                {
                    uint32_t data32 = std::stoul(value);
                    value.clear();
                    value.push_back((data32 >> 24) & 0xFF);
                    value.push_back((data32 >> 16) & 0xFF);
                    value.push_back((data32 >> 8) & 0xFF);
                    value.push_back(data32 & 0xFF);
                    new CDIRequestProcessor(socket, offs, size, target, param_type, value);
                }
            }
            else if (param_type == "eventid")
            {
                LOG(VERBOSE, "[Web] CDI EVENT WRITE offs:%d, value: %s", offs, value.c_str());
                uint64_t data = string_to_uint64(value);
                value.clear();
                value.push_back((data >> 56) & 0xFF);
                value.push_back((data >> 48) & 0xFF);
                value.push_back((data >> 40) & 0xFF);
                value.push_back((data >> 32) & 0xFF);
                value.push_back((data >> 24) & 0xFF);
                value.push_back((data >> 16) & 0xFF);
                value.push_back((data >> 8) & 0xFF);
                value.push_back(data & 0xFF);
                new CDIRequestProcessor(socket, offs, size, target, param_type, value);
            }
            cJSON_Delete(root);
            return;
        }
        else if (!strcmp(req_type->valuestring, "factory-reset"))
        {
            if (force_factory_reset())
            {
                Singleton<esp32io::DelayRebootHelper>::instance()->start();
                response = R"!^!({"resp_type":"factory-reset"})!^!";
            }
        }
        else if (!strcmp(req_type->valuestring, "reset-events"))
        {
            factory_reset_events();
            response = R"!^!({"resp_type":"reset-events"})!^!";
        }
        else if (!strcmp(req_type->valuestring, "wifi-scan"))
        {
            auto wifi = Singleton<openmrn_arduino::Esp32WiFiManager>::instance();
            response = R"!^!({"resp_type":"wifi-scan"})!^!";
            SyncNotifiable n;
            wifi->start_ssid_scan(&n);
            n.wait_for_notification();
            size_t num_found = wifi->get_ssid_scan_result_count();
            vector<string> seen_ssids;
            for (int i = 0; i < num_found; i++)
            {
                auto entry = wifi->get_ssid_scan_result(i);
                if (std::find_if(seen_ssids.begin(), seen_ssids.end(),
                    [entry](string &s) {
                        return s == (char *)entry.ssid;
                    }) != seen_ssids.end())
                {
                    // filter duplicate SSIDs
                    continue;
                }
                seen_ssids.push_back((char *)entry.ssid);
                string part = StringPrintf(
                    R"!^!({"resp_type":"wifi-entry","auth":%d,"rssi":%d,"ssid":"%s"})!^!",
                    entry.authmode, entry.rssi, http::url_encode((char *)entry.ssid).c_str());
                part += "\n";
                socket->send_text(part);
            }
            wifi->clear_ssid_scan_results();
        }
        else if (!strcmp(req_type->valuestring, "test-event"))
        {
            string value = cJSON_GetObjectItem(root, "value")->valuestring;
            uint64_t eventID = string_to_uint64(value);
            node_stack->executor()->add(new CallbackExecutable([eventID]()
            {
                node_stack->send_event(eventID);
            }));
            response = R"!^!({"resp_type":"event-test"})!^!";
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

void init_webserver(node_config_t *config, openlcb::SimpleStackBase *stack)
{
    node_cfg = config;
    node_stack = stack;
    memory_client.reset(new openlcb::MemoryConfigClient(node_stack->node(), node_stack->memory_config_handler()));

#if CONFIG_EXTERNAL_HTTP_EXECUTOR
    LOG(INFO, "[Httpd] Initializing Executor");
    xTaskCreate(http_exec_task, "httpd", http::config_httpd_server_stack_size()
              , nullptr, config_arduino_openmrn_task_priority(), nullptr);
#endif // CONFIG_EXTERNAL_HTTP_EXECUTOR
    
    LOG(INFO, "[Httpd] Initializing webserver");
    http_server.reset(new http::Httpd(
#if CONFIG_EXTERNAL_HTTP_EXECUTOR
        &http_executor,
#endif // CONFIG_EXTERNAL_HTTP_EXECUTOR
        &mdns));
    if (config->wifi_mode == WIFI_MODE_AP || config->wifi_mode == WIFI_MODE_APSTA)
    {
        const esp_app_desc_t *app_data = esp_ota_get_app_description();
        http_server->captive_portal(
            StringPrintf(CAPTIVE_PORTAL_HTML, app_data->project_name, app_data->version, app_data->project_name, app_data->project_name));
    }
    http_server->redirect_uri("/", "/index.html");
    http_server->static_uri("/index.html", indexHtmlGz, indexHtmlGz_size
                          , http::MIME_TYPE_TEXT_HTML
                          , http::HTTP_ENCODING_GZIP
                          , false);
    http_server->static_uri("/cash.min.js", cashJsGz, cashJsGz_size
                          , http::MIME_TYPE_TEXT_JAVASCRIPT
                          , http::HTTP_ENCODING_GZIP);
    http_server->static_uri("/normalize.min.css", milligramMinCssGz
                          , milligramMinCssGz_size, http::MIME_TYPE_TEXT_CSS
                          , http::HTTP_ENCODING_GZIP);
    http_server->static_uri("/milligram.min.css", normalizeMinCssGz
                          , normalizeMinCssGz_size, http::MIME_TYPE_TEXT_CSS
                          , http::HTTP_ENCODING_GZIP);
    http_server->websocket_uri("/ws", websocket_proc);
    http_server->uri("/fs", http::HttpMethod::GET, [&](http::HttpRequest *request) -> http::AbstractHttpResponse * {
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
    });
    http_server->uri("/ota", http::HttpMethod::POST, nullptr, process_ota);
}

void shutdown_webserver()
{
    LOG(INFO, "[Httpd] Shutting down webserver");
    http_server.reset(nullptr);
#if CONFIG_EXTERNAL_HTTP_EXECUTOR
    LOG(INFO, "[Httpd] Shutting down webserver executor");
    http_executor.shutdown();
#endif // CONFIG_EXTERNAL_HTTP_EXECUTOR
}