# WSLightServer Library for ESP-IDF 📡

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

## Getting Started 🚀

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
    Increase timer stack size: Component config -> FreeRTOS -> Kernel -> TIMER_TASK_STACK_DEPTH to 4096.
    ```
    

### Usage 📝

Here are some example usages of the WSLightServer library:

#### Basic WebSocket Server 🌟

```cpp

#include "ws_light_server.h"

/**
 * This function initializes and starts the WebSocket server with default parameters.
 * The server will start in Access Point mode with the default SSID "default_ssid" and 
 * password "default_password". Ping to client is enabled by default.
 */
extern "C" void app_main(void) {
    WSLightServer &server = WSLightServer::getInstance();
    server.start();
}
```

#### Echo Server Example 🔄

```cpp

#include "ws_light_server.h"

/**
 * This function initializes and starts the WebSocket server with default parameters.
 * The server will start in Access Point mode with the default SSID "default_ssid" and 
 * password "default_password". Ping/pong is enabled by default.
 * It also sets up the necessary callbacks to handle incoming WebSocket messages and 
 * implements an echo functionality.
 */
extern "C" void app_main(void) {
    WSLightServer &server = WSLightServer::getInstance();

    // Start the WebSocket server
    server.start("default_ssid", "default_password", 8080, 25000, 60000, true, []()
                 { ESP_LOGW("WSLightServer", "Test extra config"); });

    // Set the callback for text messages
    server.onTextMessage([&server](int client_sock, const std::string &message) {
        ESP_LOGI("EchoServer", "Received text message: %s", message.c_str());
        server.sendTextMessage(message);
    });

    // Set the callback for binary messages
    server.onBinaryMessage([&server](int client_sock, const std::vector<uint8_t> &message) {
        ESP_LOGI("EchoServer", "Received binary message");
        esp_log_buffer_hex("Received WS binary", message.data(), message.size());
        server.sendBinaryMessage(message.data(), message.size());
    });

    // Set the callback for when a client connects
    server.onClientConnected([](int sockfd) {
        ESP_LOGI("EchoServer", "Client connected: %d", sockfd);
    });

    // Set the callback for when a client disconnects
    server.onClientDisconnected([](int sockfd) {
        ESP_LOGI("EchoServer", "Client disconnected: %d", sockfd);
    });
}
```

## Documentation 📚

For detailed documentation, please refer to the Doxygen-generated documentation in the `.h` files.

## Contributing 🤝

Contributions are welcome! Please open an issue or submit a pull request for any improvements or bug fixes.

## License 📝

This project is licensed under the MIT License. See the LICENSE file for details.

----------

Happy coding! 🎉
