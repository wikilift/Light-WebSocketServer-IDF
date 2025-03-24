#include "ws_light_server.h"

/**
 * @brief Entry point of the application.
 *
 * This function initializes and starts the WebSocket server using custom configuration values.
 * The server operates in Access Point mode by default with:
 *  - SSID: "myLittlePony"
 *  - Password: "password123456"
 *  - Port: 4005
 *  - Ping interval: 8000 ms
 *  - Client inactivity timeout: 60000 ms
 * 
 * It also registers callbacks to:
 *  - Echo back text and binary messages.
 *  - Log client connection and disconnection events.
 * 
 * Additionally, a pre-connection hook is used to allow optional IP or Wi-Fi configuration.
 */
extern "C" void app_main(void)
{
    WSLightServer &server = WSLightServer::getInstance();

    // Handle incoming text messages with echo response
    server.onTextMessage([](int client_sock, const std::string &message)
                         {
        ESP_LOGI("EchoServer", "Received text message: %s", message.c_str());
        server.sendTextMessage(message); });

    // Handle incoming binary messages with echo response
    server.onBinaryMessage([](int client_sock, uint8_t *message, size_t length)
                           {
        ESP_LOGI("EchoServer", "Received binary message");
        esp_log_buffer_hex("EchoServer", message, length);
        server.sendBinaryMessage(message, length); });

    // Log client connections
    server.onClientConnected([](int sockfd)
                             { ESP_LOGI("EchoServer", "Client connected: %d", sockfd); });

    // Log client disconnections
    server.onClientDisconnected([](int sockfd)
                                { ESP_LOGI("EchoServer", "Client disconnected: %d", sockfd); });

    // Start the WebSocket server
    esp_err_t err = server.start(
        "myLittlePony",    // Wi-Fi SSID
        "password123456",  // Wi-Fi password
        4005,              // Server port
        8000,              // Ping interval (ms)
        60000,             // Max inactivity (ms)
        true,              // Access Point mode
        true,              // Enable ping/pong
        []()
        {
            // Optional Wi-Fi config callback (executed before starting server)
            ESP_LOGW("WSLightServer", "Custom configuration hook triggered (e.g., set static IP)");
        });
}
