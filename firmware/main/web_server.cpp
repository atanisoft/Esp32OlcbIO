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
#include "DelayRebootHelper.hxx"
#include "EventBroadcastHelper.hxx"
#include "nvs_config.hxx"
#include "NodeRebootHelper.hxx"

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

class CDIClient;

static std::unique_ptr<http::Httpd> http_server;
static MDNS mdns;
static uint64_t node_id;
static std::unique_ptr<CDIClient> cdi_client;
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
static constexpr const char *const CAPTIVE_PORTAL_HTML = R"!^!(
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

namespace esp32io
{
    void factory_reset_events();
} // namespace esp32io

struct CDIClientRequest : public CallableFlowRequestBase
{
    enum ReadCmd
    {
        READ
    };

    enum WriteCmd
    {
        WRITE
    };

    enum UpdateCompleteCmd
    {
        UPDATE_COMPLETE
    };

    void reset(ReadCmd, openlcb::NodeHandle target_node, http::WebSocketFlow *socket, uint32_t req_id, size_t offs, size_t size, string target, string type)
    {
        reset_base();
        cmd = CMD_READ;
        this->target_node = target_node;
        this->socket = socket;
        this->req_id = req_id;
        this->offs = offs;
        this->size = size;
        this->target = target;
        this->type = type;
        value.clear();
    }

    void reset(WriteCmd, openlcb::NodeHandle target_node, http::WebSocketFlow *socket, uint32_t req_id, size_t offs, size_t size, string target, string value)
    {
        reset_base();
        cmd = CMD_WRITE;
        this->target_node = target_node;
        this->socket = socket;
        this->req_id = req_id;
        this->offs = offs;
        this->size = size;
        this->target = target;
        type.clear();
        this->value = std::move(value);
    }

    void reset(UpdateCompleteCmd, openlcb::NodeHandle target_node, http::WebSocketFlow *socket, uint32_t req_id)
    {
        reset_base();
        cmd = CMD_UPDATE_COMPLETE;
        this->target_node = target_node;
        this->socket = socket;
        this->req_id = req_id;
        type.clear();
        value.clear();
    }

    enum Command : uint8_t
    {
        CMD_READ,
        CMD_WRITE,
        CMD_UPDATE_COMPLETE
    };

    Command cmd;
    http::WebSocketFlow *socket;
    openlcb::NodeHandle target_node;
    uint32_t req_id;
    size_t offs;
    size_t size;
    string target;
    string type;
    string value;
};

class CDIClient : public CallableFlow<CDIClientRequest>
{
public:
    CDIClient(Service *service, openlcb::MemoryConfigClient *memory_client)
        : CallableFlow<CDIClientRequest>(service), client_(memory_client)
    {
    }

private:
    openlcb::MemoryConfigClient *client_;

    StateFlowBase::Action entry() override
    {
        request()->resultCode = openlcb::DatagramClient::OPERATION_PENDING;
        switch (request()->cmd)
        {
        case CDIClientRequest::CMD_READ:
            LOG(VERBOSE, "[CDI:%d] Requesting %zu bytes from %s at offset %zu", request()->req_id, request()->size, uint64_to_string_hex(request()->target_node.id).c_str(), request()->offs);
            return invoke_subflow_and_wait(client_, STATE(read_complete), openlcb::MemoryConfigClientRequest::READ_PART, request()->target_node, openlcb::MemoryConfigDefs::SPACE_CONFIG, request()->offs, request()->size);
        case CDIClientRequest::CMD_WRITE:
            LOG(VERBOSE, "[CDI:%d] Writing %zu bytes to %s at offset %zu", request()->req_id, request()->size, uint64_to_string_hex(request()->target_node.id).c_str(), request()->offs);
            return invoke_subflow_and_wait(client_, STATE(write_complete), openlcb::MemoryConfigClientRequest::WRITE, request()->target_node, openlcb::MemoryConfigDefs::SPACE_CONFIG, request()->offs, request()->value);
        case CDIClientRequest::CMD_UPDATE_COMPLETE:
            LOG(VERBOSE, "[CDI:%d] Sending update-complete to %s", request()->req_id, uint64_to_string_hex(request()->target_node.id).c_str());
            return invoke_subflow_and_wait(client_, STATE(update_complete), openlcb::MemoryConfigClientRequest::UPDATE_COMPLETE, request()->target_node);
        }
        return return_with_error(openlcb::Defs::ERROR_UNIMPLEMENTED_SUBCMD);
    }

    StateFlowBase::Action read_complete()
    {
        auto b = get_buffer_deleter(full_allocation_result(client_));
        LOG(VERBOSE, "[CDI:%d] read bytes request returned with code: %d", request()->req_id, b->data()->resultCode);
        string response;
        if (b->data()->resultCode)
        {
            LOG(VERBOSE, "[CDI:%d] non-zero result code, sending error response.", request()->req_id);
            response =
                StringPrintf(
                    R"!^!({"res":"error","error":"request failed: %d","id":%d}\m)!^!", b->data()->resultCode, request()->req_id);
        }
        else
        {
            LOG(VERBOSE, "[CDI:%d] Received %zu bytes from offset %zu", request()->req_id, request()->size, request()->offs);
            if (request()->type == "str")
            {
                response =
                    StringPrintf(
                        R"!^!({"res":"field","tgt":"%s","val":"%s","type":"%s","id":%d})!^!", request()->target.c_str(), b->data()->payload.c_str(), request()->type.c_str(), request()->req_id);
            }
            else if (request()->type == "int")
            {
                uint32_t data = b->data()->payload.data()[0];
                if (request()->size == 2)
                {
                    uint16_t data16 = 0;
                    memcpy(&data16, b->data()->payload.data(), sizeof(uint16_t));
                    data = be16toh(data16);
                }
                else if (request()->size == 4)
                {
                    uint32_t data32 = 0;
                    memcpy(&data32, b->data()->payload.data(), sizeof(uint32_t));
                    data = be32toh(data32);
                }
                response =
                    StringPrintf(
                        R"!^!({"res":"field","tgt":"%s","val":"%d","type":"%s","id":%d})!^!", request()->target.c_str(), data, request()->type.c_str(), request()->req_id);
            }
            else if (request()->type == "evt")
            {
                uint64_t event_id = 0;
                memcpy(&event_id, b->data()->payload.data(), sizeof(uint64_t));
                response =
                    StringPrintf(
                        R"!^!({"res":"field","tgt":"%s","val":"%s","type":"%s","id":%d})!^!", request()->target.c_str(), uint64_to_string_hex(be64toh(event_id)).c_str(), request()->type.c_str(), request()->req_id);
            }
        }
        LOG(VERBOSE, "[CDI-READ] %s", response.c_str());
        request()->socket->send_text(response);
        return return_with_error(b->data()->resultCode);
    }

    StateFlowBase::Action write_complete()
    {
        auto b = get_buffer_deleter(full_allocation_result(client_));
        LOG(VERBOSE, "[CDI:%d] write bytes request returned with code: %d", request()->req_id, b->data()->resultCode);
        string response;
        if (b->data()->resultCode)
        {
            LOG(VERBOSE, "[CDI:%d] non-zero result code, sending error response.", request()->req_id);
            response =
                StringPrintf(
                    R"!^!({"res":"error","error":"request failed: %d","id":%d})!^!", b->data()->resultCode, request()->req_id);
        }
        else
        {
            LOG(VERBOSE, "[CDI:%d] Write request processed successfully.", request()->req_id);
            response =
                StringPrintf(R"!^!({"res":"saved","tgt":"%s","id":%d})!^!", request()->target.c_str(), request()->req_id);
        }
        LOG(VERBOSE, "[CDI-WRITE] %s", response.c_str());
        request()->socket->send_text(response);
        return return_with_error(b->data()->resultCode);
    }

    StateFlowBase::Action update_complete()
    {
        auto b = get_buffer_deleter(full_allocation_result(client_));
        LOG(VERBOSE, "[CDI:%d] update-complete request returned with code: %d", request()->req_id, b->data()->resultCode);
        string response;
        if (b->data()->resultCode)
        {
            LOG(VERBOSE, "[CDI:%d] non-zero result code, sending error response.", request()->req_id);
            response =
                StringPrintf(
                    R"!^!({"res":"error","error":"request failed: %d","id":%d}\m)!^!", b->data()->resultCode, request()->req_id);
        }
        else
        {
            LOG(VERBOSE, "[CDI:%d] update-complete request processed successfully.", request()->req_id);
            response =
                StringPrintf(R"!^!({"res":"update-complete","id":%d})!^!", request()->req_id);
        }
        LOG(VERBOSE, "[CDI-UPDATE-COMPLETE] %s", response.c_str());
        request()->socket->send_text(response);
        return return_with_error(b->data()->resultCode);
    }
};

WEBSOCKET_STREAM_HANDLER_IMPL(websocket_proc, socket, event, data, len)
{
    if (event == http::WebSocketEvent::WS_EVENT_TEXT)
    {
        string response = R"!^!({"res":"error","error":"Request not understood"})!^!";
        string req = string((char *)data, len);
        LOG(VERBOSE, "[WS] MSG: %s", req.c_str());
        cJSON *root = cJSON_Parse(req.c_str());
        cJSON *req_type = cJSON_GetObjectItem(root, "req");
        cJSON *req_id = cJSON_GetObjectItem(root, "id");
        if (req_type == NULL || req_id == NULL)
        {
            // NO OP, the websocket is outbound only to trigger events on the client side.
            LOG(INFO, "[WSJSON] Failed to parse:%s", req.c_str());
        }
        else if (!strcmp(req_type->valuestring, "nodeid"))
        {
            if (!cJSON_HasObjectItem(root, "val"))
            {
                response =
                    StringPrintf(R"!^!({"res":"error","error":"The 'val' field must be provided","id":%d})!^!", req_id->valueint);
            }
            else
            {
                std::string value = cJSON_GetObjectItem(root, "val")->valuestring;
                if (set_node_id(string_to_uint64(value)))
                {
                    LOG(INFO, "[WSJSON:%d] Node ID updated to: %s, reboot pending", req_id->valueint, value.c_str());
                    response =
                        StringPrintf(R"!^!({"res":"nodeid","id":%d})!^!", req_id->valueint);
                    Singleton<esp32io::NodeRebootHelper>::instance()->reboot();
                }
                else
                {
                    LOG(INFO, "[WSJSON:%d] Node ID update failed", req_id->valueint);
                    response =
                        StringPrintf(R"!^!({"res":"error","error":"Failed to update node-id","id":%d})!^!", req_id->valueint);
                }
            }
        }
        else if (!strcmp(req_type->valuestring, "info"))
        {
            const esp_app_desc_t *app_data = esp_ota_get_app_description();
            const esp_partition_t *partition = esp_ota_get_running_partition();
            response =
                StringPrintf(R"!^!({"res":"info","timestamp":"%s %s","ota":"%s","snip_name":"%s","snip_hw":"%s","snip_sw":"%s","node_id":"%s","twai":%s,"pwm":%s,"id":%d})!^!"
                           , app_data->date, app_data->time, partition->label
                           , openlcb::SNIP_STATIC_DATA.model_name
                           , openlcb::SNIP_STATIC_DATA.hardware_version
                           , openlcb::SNIP_STATIC_DATA.software_version
                           , uint64_to_string_hex(node_id).c_str()
#if CONFIG_OLCB_ENABLE_TWAI
                           , "true"
#else
                           , "false"
#endif // CONFIG_OLCB_ENABLE_TWAI
#if CONFIG_OLCB_ENABLE_PWM
                           , "true"
#else
                           , "false"
#endif // CONFIG_OLCB_ENABLE_PWM
                           , req_id->valueint);
        }
        else if (!strcmp(req_type->valuestring, "update-complete"))
        {
            BufferPtr<CDIClientRequest> b(cdi_client->alloc());
            b->data()->reset(CDIClientRequest::UPDATE_COMPLETE, node_handle, socket, req_id->valueint);
            b->data()->done.reset(EmptyNotifiable::DefaultInstance());
            LOG(VERBOSE, "[WSJSON:%d] Sending UPDATE_COMPLETE to queue", req_id->valueint);
            cdi_client->send(b->ref());
            cJSON_Delete(root);
            return;
        }
        else if (!strcmp(req_type->valuestring, "cdi"))
        {
            if (!cJSON_HasObjectItem(root, "ofs") ||
                !cJSON_HasObjectItem(root, "type") ||
                !cJSON_HasObjectItem(root, "sz") ||
                !cJSON_HasObjectItem(root, "tgt"))
            {
                LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s"
                        , req_id->valueint, req.c_str());
                response =
                    StringPrintf(
                        R"!^!({"res":"error", "error":"request is missing one (or more) required parameters","id":%d})!^!"
                      , req_id->valueint);
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
                    LOG(VERBOSE
                      , "[WSJSON:%d] Sending CDI READ: offs:%zu size:%zu type:%s tgt:%s"
                      , req_id->valueint, offs, size, param_type.c_str()
                      , target.c_str());
                    b->data()->reset(CDIClientRequest::READ, node_handle
                                   , socket, req_id->valueint, offs, size
                                   , target, param_type);
                }
                else
                {
                    string value = "";
                    cJSON *raw_value = cJSON_GetObjectItem(root, "val");
                    if (param_type == "str")
                    {
                        string encoded_value =
                            http::url_decode(raw_value->valuestring);
                        // copy of up to the reported size.
                        value = string(encoded_value, size);
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
                        uint64_t data = string_to_uint64(string(raw_value->valuestring));
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
                    LOG(VERBOSE
                      , "[WSJSON:%d] Sending CDI WRITE: offs:%zu value:%s tgt:%s"
                      , req_id->valueint, offs, raw_value->valuestring
                      , target.c_str());
                    b->data()->reset(CDIClientRequest::WRITE, node_handle
                                   , socket, req_id->valueint, offs, size
                                   , target, value);
                }
                b->data()->done.reset(EmptyNotifiable::DefaultInstance());
                cdi_client->send(b->ref());
                cJSON_Delete(root);
                return;
            }
        }
        else if (!strcmp(req_type->valuestring, "factory-reset"))
        {
            LOG(VERBOSE, "[WSJSON:%d] Factory reset received", req_id->valueint);
            if (force_factory_reset())
            {
                Singleton<esp32io::NodeRebootHelper>::instance()->reboot();
                response =
                    StringPrintf(R"!^!({"res":"factory-reset","id":%d})!^!"
                               , req_id->valueint);
            }
            else
            {
                LOG(INFO, "[WSJSON:%d] Factory reset update failed", req_id->valueint);
                response =
                    StringPrintf(R"!^!({"res":"error","error":"Failed to record factory reset request","id":%d})!^!"
                               , req_id->valueint);
            }
        }
        else if (!strcmp(req_type->valuestring, "bootloader"))
        {
            LOG(VERBOSE, "[WSJSON:%d] bootloader request received"
              , req_id->valueint);
            enter_bootloader();
            // NOTE: This response may not get sent to the client.
            response =
                StringPrintf(R"!^!({"res":"bootloader","id":%d})!^!"
                           , req_id->valueint);
        }
        else if (!strcmp(req_type->valuestring, "reset-events"))
        {
            LOG(VERBOSE, "[WSJSON:%d] Reset event IDs received"
              , req_id->valueint);
            esp32io::factory_reset_events();
            response =
                StringPrintf(R"!^!({"res":"reset-events","id":%d})!^!"
                           , req_id->valueint);
        }
        else if (!strcmp(req_type->valuestring, "event"))
        {
            if (!cJSON_HasObjectItem(root, "evt"))
            {
                LOG_ERROR("[WSJSON:%d] One or more required parameters are missing: %s"
                        , req_id->valueint, req.c_str());
                response =
                    StringPrintf(R"!^!({"res":"error","error":"The 'evt' field must be provided","id":%d})!^!"
                               , req_id->valueint);
            }
            else
            {
                string value = cJSON_GetObjectItem(root, "evt")->valuestring;
                LOG(VERBOSE, "[WSJSON:%d] Sending event: %s", req_id->valueint
                  , value.c_str());
                uint64_t eventID = string_to_uint64(value);
                Singleton<esp32io::EventBroadcastHelper>::instance()->send_event(eventID);
                response =
                    StringPrintf(R"!^!({"res":"event","evt":"%s","id":%d})!^!"
                               , value.c_str(), req_id->valueint);
            }
        }
        else if (!strcmp(req_type->valuestring, "ping"))
        {
            LOG(VERBOSE, "[WSJSON:%d] PING received", req_id->valueint);
            response =
                StringPrintf(R"!^!({"res":"pong","id":%d})!^!"
                           , req_id->valueint);
        }
        else
        {
            LOG_ERROR("Unrecognized request: %s", req.c_str());
        }
        cJSON_Delete(root);
        LOG(VERBOSE, "[Web] WS: %s -> %s", req.c_str(), response.c_str());
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

namespace openlcb
{
    extern const char CDI_DATA[];
    extern const size_t CDI_SIZE;
} // namespace openlcb

void init_webserver(openlcb::MemoryConfigClient *cfg_client, Service *service, uint64_t id)
{
    const esp_app_desc_t *app_data = esp_ota_get_app_description();
    node_id = id;
    node_handle = openlcb::NodeHandle(id);
    cdi_client.reset(new CDIClient(service, cfg_client));
    LOG(INFO, "[Httpd] Initializing webserver");
    http_server.reset(new http::Httpd(&mdns));
    http_server->redirect_uri("/", "/index.html");
    http_server->static_uri("/index.html", indexHtmlGz, indexHtmlGz_size, http::MIME_TYPE_TEXT_HTML, http::HTTP_ENCODING_GZIP, false);
    http_server->static_uri("/cash.min.js", cashJsGz, cashJsGz_size, http::MIME_TYPE_TEXT_JAVASCRIPT, http::HTTP_ENCODING_GZIP);
    http_server->static_uri("/spectre.min.css", spectreMinCssGz, spectreMinCssGz_size, http::MIME_TYPE_TEXT_CSS, http::HTTP_ENCODING_GZIP);
    http_server->static_uri("/cdi.xml", (const uint8_t *)openlcb::CDI_DATA, openlcb::CDI_SIZE, http::MIME_TYPE_TEXT_XML);
    http_server->websocket_uri("/ws", websocket_proc);
    http_server->uri("/fs", http::HttpMethod::GET, fs_proc);
    http_server->uri("/ota", http::HttpMethod::POST, nullptr, process_ota);
    http_server->captive_portal(
        StringPrintf(CAPTIVE_PORTAL_HTML, app_data->project_name, app_data->version, app_data->project_name, app_data->project_name));
}

void shutdown_webserver()
{
    LOG(INFO, "[Httpd] Shutting down webserver");
    http_server.reset(nullptr);
}