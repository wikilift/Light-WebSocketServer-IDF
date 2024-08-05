/**
 * @file ws_light_server.cpp
 * @brief WSLightServer server class implementation.
 * 
 * This file defines the WSLightServer class implementation.
 * 
 * @author Daniel Giménez
 * @date 2024-08-05
 * @license MIT License
 * 
 * @par License:
 * 
 * MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include "ws_light_server.h"
#include <cstring>
#include <lwip/netdb.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>

WSLightServer *WSLightServer::instance = nullptr;

WSLightServer &WSLightServer::getInstance()
{
    if (instance == nullptr)
    {
        instance = new WSLightServer();
    }
    return *instance;
}

WSLightServer::WSLightServer()
    : server_sock(-1), client_sock(-1), ping_pong_enabled(true)
{
}

esp_err_t WSLightServer::start(const char *ssid,
                               const char *password,
                               uint16_t port,
                               uint64_t ping_interval_ms,
                               uint64_t max_inactivity_ms,
                               bool enable_ping_pong,

                               std::function<void()> extra_config)
{
    this->max_inactivity_ms = max_inactivity_ms;
    this->ping_pong_enabled = enable_ping_pong;
    this->ping_interval_ms = ping_interval_ms;
    this->port = port;

    if (wifi_init(ssid, password, extra_config) == ESP_OK)
    {
        xTaskCreatePinnedToCore(&WSLightServer::handle_client_wrapper, "ws_client_handler", 6048, this, 8, NULL, tskNO_AFFINITY);
    }
    else
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void WSLightServer::handle_client_wrapper(void *arg)
{
    WSLightServer *server = static_cast<WSLightServer *>(arg);
    server->handle_client();
}

void WSLightServer::onTextMessage(std::function<void(int, const std::string &)> callback)
{
    text_message_callback = callback;
}

void WSLightServer::onBinaryMessage(std::function<void(int, const std::vector<uint8_t> &)> callback)
{
    binary_message_callback = callback;
}

void WSLightServer::onPingMessage(std::function<void(int)> callback)
{
    ping_message_callback = callback;
}

void WSLightServer::onPongMessage(std::function<void(int)> callback)
{
    pong_message_callback = callback;
}

void WSLightServer::onCloseMessage(std::function<void(int)> callback)
{
    close_message_callback = callback;
}

void WSLightServer::onClientConnected(std::function<void(int)> callback)
{
    client_connected_callback = callback;
}

void WSLightServer::onClientDisconnected(std::function<void(int)> callback)
{
    client_disconnected_callback = callback;
    client_sock = -1;
}

esp_err_t WSLightServer::wifi_init(const char *ssid, const char *password, std::function<void()> extra_config)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {};
    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));
    strncpy((char *)ap_config.ap.password, password, sizeof(ap_config.ap.password));
    ap_config.ap.max_connection = 1;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    if (strlen(password) == 0)
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    if (extra_config)
    {
        extra_config();
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WSLightServer", "Wi-Fi initialized. SSID:%s password:%s", ssid, password);
    return ESP_OK;
}

void WSLightServer::send_ping(TimerHandle_t xTimer)
{
    WSLightServer *server = static_cast<WSLightServer *>(pvTimerGetTimerID(xTimer));
    if (server->client_sock > 0)
    {
        std::vector<uint8_t> ping_frame = server->encode_frame("", HTTPD_WS_TYPE_PING);
        send(server->client_sock, ping_frame.data(), ping_frame.size(), 0);
        if (debug)
        {
            ESP_LOGI("WSLightServer", "Sending ping to client: %d", server->client_sock);
        }
    }
}

esp_err_t WSLightServer::sendBinaryMessage(const uint8_t *data, size_t length)
{
    if (length > MAX_MESSAGE_SIZE)
    {
        ESP_LOGE("WSLightServer", "Message too large to send");
        return ESP_FAIL;
    }

    if (client_sock > 0)
    {
        std::vector<uint8_t> frame = encode_frame(std::string(data, data + length), HTTPD_WS_TYPE_BINARY);
        if (send(client_sock, frame.data(), frame.size(), 0) < 0)
        {
            ESP_LOGE("WSLightServer", "Failed to send binary message: errno %d", errno);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t WSLightServer::sendTextMessage(const std::string &text)
{
    size_t length = text.size();
    if (length > MAX_MESSAGE_SIZE)
    {
        ESP_LOGE("WSLightServer", "Message too large to send");
        return ESP_FAIL;
    }

    if (client_sock > 0)
    {
        std::vector<uint8_t> frame = encode_frame(text, HTTPD_WS_TYPE_TEXT);
        if (send(client_sock, frame.data(), frame.size(), 0) < 0)
        {
            ESP_LOGE("WSLightServer", "Failed to send text message: errno %d", errno);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

void WSLightServer::handle_client()
{
    for (;;)
    {
        server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0)
        {
            ESP_LOGE("WSLightServer", "Unable to create socket: errno %d", errno);
            vTaskDelete(nullptr);
            return;
        }

        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        int err = bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (err != 0)
        {
            ESP_LOGE("WSLightServer", "Socket unable to bind: errno %d", errno);
            close(server_sock);
            vTaskDelete(nullptr);
            return;
        }

        err = listen(server_sock, 1);
        if (err != 0)
        {
            ESP_LOGE("WSLightServer", "Error occurred during listen: errno %d", errno);
            close(server_sock);
            vTaskDelete(nullptr);
            return;
        }

        ESP_LOGI("WSLightServer", "Server listening on port %d", port);

        if (ping_pong_enabled)
        {
            ping_timer = xTimerCreate("PingTimer", pdMS_TO_TICKS(ping_interval_ms), pdTRUE, this, &WSLightServer::send_ping);
            if (ping_timer == nullptr)
            {
                ESP_LOGE("WSLightServer", "Failed to create ping timer");
                close(server_sock);
                vTaskDelete(nullptr);
                return;
            }
            if (xTimerStart(ping_timer, 0) != pdPASS)
            {
                ESP_LOGE("WSLightServer", "Failed to start ping timer");
                close(server_sock);
                vTaskDelete(nullptr);
                return;
            }
        }

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_sock < 0)
        {
            ESP_LOGE("WSLightServer", "Unable to accept connection: errno %d", errno);
            close(server_sock);
            continue;
        }

        if (client_connected_callback)
        {
            client_connected_callback(client_sock);
        }
        else
        {
            ESP_LOGI("WSLightServer", "Client connected: %d", client_sock);
        }

        char buffer[MAX_MESSAGE_SIZE];
        fd_set read_fds;
        struct timeval timeout;

        int len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

        if (len < 0)
        {
            ESP_LOGE("WSLightServer", "recv failed: errno %d", errno);
            close(client_sock);
            close(server_sock);
            continue;
        }
        if (len >= MAX_MESSAGE_SIZE)
        {
            ESP_LOGE("WSLightServer", "Received message too large, closing connection");
            close(client_sock);
            close(server_sock);
            continue;
        }

        buffer[len] = '\0';
        send_handshake(client_sock, std::string(buffer, len));

        for (;;)
        {
            FD_ZERO(&read_fds);
            FD_SET(client_sock, &read_fds);

            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int activity = select(client_sock + 1, &read_fds, NULL, NULL, &timeout);

            if (activity < 0)
            {
                ESP_LOGE("WSLightServer", "select error: errno %d", errno);
                break;
            }

            if (activity == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            if (FD_ISSET(client_sock, &read_fds))
            {
                len = recv(client_sock, buffer, sizeof(buffer), 0);
                if (len < 0)
                {
                    ESP_LOGE("WSLightServer", "recv failed: errno %d", errno);
                    break;
                }
                if (len == 0)
                {
                    ESP_LOGI("WSLightServer", "Client %d closed connection", client_sock);
                    break;
                }

                if (len >= MAX_MESSAGE_SIZE)
                {
                    ESP_LOGE("WSLightServer", "Received message too large, closing connection");
                    break;
                }

                std::vector<uint8_t> frame(buffer, buffer + len);
                ws_type_t type;
                auto decoded = decode_frame(frame, type);

                switch (type)
                {
                case HTTPD_WS_TYPE_TEXT:
                    if (text_message_callback)
                    {
                        text_message_callback(client_sock, std::string((char *)decoded.data, decoded.length));
                    }
                    else
                    {
                        ESP_LOGI("WSLightServer", "Received text message: %s",(std::string((char *)decoded.data, decoded.length).c_str()));
                    }
                    break;
                case HTTPD_WS_TYPE_BINARY:
                    if (binary_message_callback)
                    {
                        binary_message_callback(client_sock, std::vector<uint8_t>((uint8_t *)decoded.data, (uint8_t *)decoded.data + decoded.length));
                    }
                    else
                    {
                        ESP_LOGI("WSLightServer", "Received binary message");
                        esp_log_buffer_hex("WSLightServer", (uint8_t *)decoded.data, decoded.length);
                    }
                    break;
                case HTTPD_WS_TYPE_PING:
                {
                    std::vector<uint8_t> ping_data(static_cast<uint8_t *>(decoded.data), static_cast<uint8_t *>(decoded.data) + decoded.length);
                    std::vector<uint8_t> pong_frame = encode_frame(ping_data, HTTPD_WS_TYPE_PONG);
                    send(client_sock, pong_frame.data(), pong_frame.size(), 0);
                    if (ping_message_callback)
                    {
                        ping_message_callback(client_sock);
                    }
                    else
                    {
                        if (debug)
                        {
                            ESP_LOGI("WSLightServer", "Received ping from client %d", client_sock);
                            esp_log_buffer_hex("Ping Data", ping_data.data(), ping_data.size());
                            ESP_LOGW("WSLightServer", "Sending pong frame:");
                            esp_log_buffer_hex("WSLightServer", pong_frame.data(), pong_frame.size());
                        }
                    }
                }
                break;

                    break;
                case HTTPD_WS_TYPE_PONG:
                    if (pong_message_callback)
                    {
                        pong_message_callback(client_sock);
                    }
                    else
                    {
                        if (debug)
                        {
                            ESP_LOGI("WSLightServer", "Received pong from client %d", client_sock);
                        }
                    }
                    break;
                case HTTPD_WS_TYPE_CLOSE:
                    ESP_LOGI("WSLightServer", "Received close frame from client %d", client_sock);
                    if (close_message_callback)
                    {
                        close_message_callback(client_sock);
                    }
                    goto cleanup;
                default:
                    ESP_LOGW("WSLightServer", "Unknown WS frame type %d", type);
                    break;
                }
                if (decoded.data != nullptr)
                {
                    vPortFree(decoded.data);
                }
            }
        }

    cleanup:
        if (client_disconnected_callback)
        {
            client_disconnected_callback(client_sock);
        }
        else
        {
            ESP_LOGI("WSLightServer", "Client disconnected: %d", client_sock);
        }

        close(client_sock);
        client_sock = -1;
        close(server_sock);
    }
}

void WSLightServer::send_handshake(int client_sock, const std::string &request)
{
    size_t key_start = request.find("Sec-WebSocket-Key: ") + 19;
    size_t key_end = request.find("\r\n", key_start);
    std::string key = request.substr(key_start, key_end - key_start);

    key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha1_result[20];
    mbedtls_sha1(reinterpret_cast<const unsigned char *>(key.c_str()), key.length(), sha1_result);

    unsigned char base64_result[64];
    size_t base64_len;
    mbedtls_base64_encode(base64_result, sizeof(base64_result), &base64_len, sha1_result, sizeof(sha1_result));

    std::string handshake = "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: " +
                            std::string(reinterpret_cast<char *>(base64_result), base64_len) + "\r\n\r\n";

    send(client_sock, handshake.c_str(), handshake.length(), 0);
}

WSLightServer::DecodedMessage WSLightServer::decode_frame(const std::vector<uint8_t> &frame, ws_type_t &type)
{
    uint8_t first_byte = frame[0];
    uint8_t second_byte = frame[1];
    type = (ws_type_t)(first_byte & 0x0F);

    size_t payload_len = second_byte & 0x7F;
    size_t mask_start = 2;

    if (payload_len == 126)
    {
        mask_start += 2;
        payload_len = (frame[2] << 8) | frame[3];
    }
    else if (payload_len == 127)
    {
        mask_start += 8;
        payload_len = 0;
        for (int i = 0; i < 8; ++i)
        {
            payload_len = (payload_len << 8) | frame[2 + i];
        }
    }

    size_t mask_key[4] = {frame[mask_start], frame[mask_start + 1], frame[mask_start + 2], frame[mask_start + 3]};
    size_t payload_start = mask_start + 4;

    void *message = pvPortMalloc(payload_len);
    for (size_t i = 0; i < payload_len; ++i)
    {
        ((uint8_t *)message)[i] = frame[payload_start + i] ^ mask_key[i % 4];
    }

    return {message, payload_len};
}

std::vector<uint8_t> WSLightServer::encode_frame(const std::vector<uint8_t> &message, ws_type_t type)
{
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | type);

    size_t message_size = message.size();
    if (message_size <= 125)
    {
        frame.push_back(static_cast<uint8_t>(message_size));
    }
    else if (message_size <= 65535)
    {
        frame.push_back(126);
        frame.push_back((message_size >> 8) & 0xFF);
        frame.push_back(message_size & 0xFF);
    }
    else
    {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
        {
            frame.push_back((message_size >> (i * 8)) & 0xFF);
        }
    }

    frame.insert(frame.end(), message.begin(), message.end());
    return frame;
}

std::vector<uint8_t> WSLightServer::encode_frame(const std::string &message, ws_type_t type)
{
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | type);

    if (message.size() <= 125)
    {
        frame.push_back(message.size());
    }
    else if (message.size() <= 65535)
    {
        frame.push_back(126);
        frame.push_back((message.size() >> 8) & 0xFF);
        frame.push_back(message.size() & 0xFF);
    }
    else
    {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
        {
            frame.push_back((message.size() >> (i * 8)) & 0xFF);
        }
    }

    frame.insert(frame.end(), message.begin(), message.end());
    return frame;
}

ws_client_info_t WSLightServer::get_client_info(const std::string &request)
{
    if (request.find("Upgrade: websocket") != std::string::npos)
    {
        return HTTPD_WS_CLIENT_WEBSOCKET;
    }
    else
    {
        return HTTPD_WS_CLIENT_HTTP;
    }
}