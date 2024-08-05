/**
 * @file ws_light_server.h
 * @brief WSLightServer server class for handling WebSocket connection and messages.
 * 
 * This file defines the WSLightServer class, which provides a WebSocket server implementation
 * for the ESP32 using the ESP-IDF framework. It includes functionality for handling text
 * and binary messages, managing client connections, and performing keep-alive checks.
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

#pragma once

#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <string>
#include <vector>
#include <functional>
#include "ws_types.h"
#include "freertos/timers.h"

#define MAX_MESSAGE_SIZE 1024

/**
 * @struct ws_message_t
 * @brief Structure to hold a WebSocket message.
 */
struct ws_message_t
{
    int client_sock;                 /**< Socket of the client */
    uint8_t data[MAX_MESSAGE_SIZE];  /**< Data of the message */
    size_t length;                   /**< Length of the message */
    ws_type_t type;                  /**< Type of the message */
    bool broadcast;                  /**< Broadcast flag */
};

/**
 * @class WSLightServer
 * @brief Singleton class to handle WebSocket server functionality.
 */
class WSLightServer
{
public:
    /**
     * @brief Get the singleton instance of the WSLightServer.
     * @return Reference to the singleton instance.
     */
    static WSLightServer &getInstance();

    /**
     * @brief Start the WebSocket server.
     * @param ssid WiFi SSID.
     * @param password WiFi password.
     * @param port Server port.
     * @param ping_interval_ms Interval for sending ping messages to client in milliseconds.
     * @param max_inactivity_ms Maximum inactivity period in milliseconds.
     * @param enable_ping_pong Flag to enable ping messages to client.
     * @param extra_config Extra configuration callback.
     * @return ESP_OK on success, an error code otherwise.
     */
    esp_err_t start(const char *ssid = "default_ssid",
                    const char *password = "default_password",
                    uint16_t port = 80,
                    uint64_t ping_interval_ms = 30000,
                    uint64_t max_inactivity_ms = 60000,
                    bool enable_ping_pong = true,
                    std::function<void()> extra_config = nullptr);

    /**
     * @brief Set the callback for handling text messages.
     * @param callback Function to handle text messages.
     */
    void onTextMessage(std::function<void(int, const std::string &)> callback);

    /**
     * @brief Set the callback for handling binary messages.
     * @param callback Function to handle binary messages.
     */
    void onBinaryMessage(std::function<void(int, const std::vector<uint8_t> &)> callback);

    /**
     * @brief Set the callback for handling ping messages.
     * @param callback Function to handle ping messages.
     */
    void onPingMessage(std::function<void(int)> callback);

    /**
     * @brief Set the callback for handling pong messages.
     * @param callback Function to handle pong messages.
     */
    void onPongMessage(std::function<void(int)> callback);

    /**
     * @brief Set the callback for handling close messages.
     * @param callback Function to handle close messages.
     */
    void onCloseMessage(std::function<void(int)> callback);

    /**
     * @brief Set the callback for handling client connections.
     * @param callback Function to handle client connections.
     */
    void onClientConnected(std::function<void(int)> callback);

    /**
     * @brief Set the callback for handling client disconnections.
     * @param callback Function to handle client disconnections.
     */
    void onClientDisconnected(std::function<void(int)> callback);

    /**
     * @brief Send a text message to the client.
     * @param text The text message to send.
     * @return ESP_OK on success, an error code otherwise.
     */
    esp_err_t sendTextMessage(const std::string &text);

    /**
     * @brief Send a binary message to the client.
     * @param data The binary data to send.
     * @param length The length of the binary data.
     * @return ESP_OK on success, an error code otherwise.
     */
    esp_err_t sendBinaryMessage(const uint8_t *data, size_t length);

private:
    /**
     * @struct DecodedMessage
     * @brief Structure to hold a decoded WebSocket message.
     */
    struct DecodedMessage
    {
        void *data;   /**< Pointer to the message data */
        size_t length;/**< Length of the message data */
    };

    /**
     * @brief Constructor for WSLightServer.
     */
    WSLightServer();

    /**
     * @brief Initialize WiFi.
     * @param ssid WiFi SSID.
     * @param password WiFi password.
     * @param extra_config Extra configuration callback.
     * @return ESP_OK on success, an error code otherwise.
     */
    esp_err_t wifi_init(const char *ssid,
                        const char *password,
                        std::function<void()> extra_config = nullptr);

    /**
     * @brief Handle client connections.
     */
    void handle_client();

    /**
     * @brief Send handshake to the client.
     * @param client_sock Client socket.
     * @param request Handshake request.
     */
    void send_handshake(int client_sock, const std::string &request);

    /**
     * @brief Decode a WebSocket frame.
     * @param frame The WebSocket frame.
     * @param type The type of WebSocket message.
     * @return DecodedMessage containing the decoded message.
     */
    DecodedMessage decode_frame(const std::vector<uint8_t> &frame, ws_type_t &type);

    /**
     * @brief Encode a message into a WebSocket frame.
     * @param message The message to encode.
     * @param type The type of WebSocket message.
     * @return Vector containing the encoded WebSocket frame.
     */
    std::vector<uint8_t> encode_frame(const std::string &message, ws_type_t type = HTTPD_WS_TYPE_TEXT);

    /**
     * @brief Encode a message into a WebSocket frame.
     * @param message The message to encode.
     * @param type The type of WebSocket message.
     * @return Vector containing the encoded WebSocket frame.
     */
    std::vector<uint8_t> encode_frame(const std::vector<uint8_t>& message, ws_type_t type);

    /**
     * @brief Send a ping message.
     * @param xTimer The timer handle.
     */
    static void send_ping(TimerHandle_t xTimer);

    /**
     * @brief Wrapper for handling client connections.
     * @param arg Argument passed to the task.
     */
    static void handle_client_wrapper(void *arg);

    /**
     * @brief Get client information from the request.
     * @param request The client request.
     * @return Client information.
     */
    ws_client_info_t get_client_info(const std::string &request);

    uint16_t port;                    /**< Server port */
    int server_sock;                  /**< Server socket */
    int client_sock;                  /**< Client socket */
    TimerHandle_t ping_timer;         /**< Timer handle for ping messages */
    uint64_t max_inactivity_ms;       /**< Maximum inactivity period in milliseconds */
    bool ping_pong_enabled;           /**< Flag to enable ping messages to client */
    size_t ping_interval_ms;          /**< Interval for sending ping messages in milliseconds */
    char* user;                       /**< Username */
    char* pwd;                        /**< Password */

    std::function<void(int, const std::string &)> text_message_callback;  /**< Callback for text messages */
    std::function<void(int, const std::vector<uint8_t> &)> binary_message_callback; /**< Callback for binary messages */
    std::function<void(int)> ping_message_callback; /**< Callback for ping messages */
    std::function<void(int)> pong_message_callback; /**< Callback for pong messages */
    std::function<void(int)> close_message_callback; /**< Callback for close messages */
    std::function<void(int)> client_connected_callback; /**< Callback for client connections */
    std::function<void(int)> client_disconnected_callback; /**< Callback for client disconnections */

    static WSLightServer *instance;   /**< Singleton instance */
    static const constexpr bool debug = false; /**< Debug flag */
};
