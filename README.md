# WSLightServer Library for ESP-IDF 

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v4.4+-blue.svg)](https://github.com/espressif/esp-idf)


The objective of this library was to obtain a WebSocket server for a **single client** in the lightest way possible, being within the IDF environment without using third-party libraries or using esp_http_server, which, despite being a masterful implementation, considerably increased the binary. 
For a multi-client implementation with ssl please visit my other repository

[![WSServer](https://img.shields.io/badge/GitHub-WSServer%20Repo-blue?logo=github)](https://github.com/wikilift/WebSocket-server-ESP-IDF)
## Features ✨

-   **WebSocket Server:** Handles WebSocket connection for one client efficiently avoiding use of esp_http server.
-   **Ping/Pong Support:** Ensures client remain connected and responsive.
-   **Callbacks:** Customizable callbacks for handling text, binary, ping, pong, and close messages, as well as client connect and disconnect events.
-   **Single Client Support:** This implementation supports one client at a time.
-   **Lightweight footprint:** Minimal memory usage, no heap leaks.
-   **Thread-safe API:** all callbacks run within FreeRTOS context.


## Getting Started 

### Prerequisites 📋

-   ESP-IDF v4.4 or later.
-   ESP32 development board.
-   CMake build system.

### Installation 📥

1.  **Clone the repository:**

	```sh
	git clone https://github.com/yourusername/ws_light_server.git`
	```

2.  **Add WSLightServer as a component to your ESP-IDF project:** 

	Copy the `ws_light_server` directory into the `components` directory of your ESP-IDF project.
    
4.  **Configure your project:** Make sure to increase the timer stack size in your project configuration:
    
    ```sh
    idf.py menuconfig
    Enable WebSocket support: Component config -> ESP HTTP server -> Enable ESP_HTTPS_SERVER component.
    ```
    

### Usage 📝

Here are some example usages of the WSLightServer library:

#### Basic WebSocket Server 

```cpp
#include "ws_light_server.h"

/**
 * /**
 * @author Daniel Giménez
 * @date 2024-08-05
 * @date updated 2025-03-24 - Full revamp
 * @license MIT License
 * @brief Minimal startup example for the WebSocket server.
 *
 * This function initializes and starts the WebSocket server using default parameters.
 * By default:
 * - The server runs in Access Point mode.
 * - SSID: "Wikilift ssid"
 * - Password: "myAwesomePwd123456"
 * - Port: 80
 * - Ping interval: 6000 ms
 * - Inactivity timeout: 50000 ms
 * - Ping/pong keep-alive is enabled.
 *
 * No callbacks are registered in this minimal example.
 */
extern "C" void app_main(void) {
    WSLightServer &server = WSLightServer::getInstance();
    server.start();
}

```

#### Echo Server Example 

```cpp
#include "ws_light_server.h"

/**
 * @author Daniel Giménez
 * @date 2024-08-05
 * @date updated 2025-03-24 - Full revamp
 * @license MIT License
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

```

## Documentation 📚

📖 [View online API documentation](https://github.com/wikilift/Light-WebSocketServer-IDF/blob/master/docs/html/index.html)



## Contributing 🤝

Contributions are welcome! Please open an issue or submit a pull request for any improvements or bug fixes.

## License 📝

This project is licensed under the MIT License. See the LICENSE file for details.

----------

Happy coding! 🎉