#include "ws_light_server.h"
#include "mbedtls/base64.h"

/**
 * @brief Task that performs intensive mathematical calculations.
 *
 * This task calculates the factorial of 20 in an infinite loop.
 *
 * @param pvp Task parameter (not used).
 */
void mathTask(void *pvp)
{
    for (;;)
    {
        volatile long long factorial = 1;
        for (int i = 1; i <= 20; ++i)
        {
            factorial *= i;
        }
        // ESP_LOGW("MathTask", "Factorial of 20: %lld", factorial);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Task that handles large arrays.
 *
 * This task creates an array of 1000 elements, fills it, and sorts it in an infinite loop.
 *
 * @param pvp Task parameter (not used).
 */
void arrayTask(void *pvp)
{
    for (;;)
    {
        const int size = 1000;
        int array[size];
        for (int i = 0; i < size; ++i)
        {
            array[i] = size - i;
        }
        std::sort(array, array + size);
        // ESP_LOGW("ArrayTask", "Array sorted");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief Task that performs data compression.
 *
 * This task encodes a string into base64 format in an infinite loop.
 *
 * @param pvp Task parameter (not used).
 */
void compressionTask(void *pvp)
{
    for (;;)
    {
        const char *input = "The quick brown fox jumps over the lazy dog";
        size_t input_size = strlen(input);
        size_t output_size = 2 * input_size;
        char output[output_size];

        mbedtls_base64_encode(reinterpret_cast<unsigned char *>(output), output_size, &output_size,
                              reinterpret_cast<const unsigned char *>(input), input_size);

        // ESP_LOGW("CompressionTask", "Data compressed");
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

/**
 * @brief Task that sends WebSocket messages.
 *
 * This task sends text and binary messages through a WebSocket server in an infinite loop.
 *
 * @param pvp Task parameter (not used).
 */
void nepe(void *pvp)
{
    WSLightServer &server = WSLightServer::getInstance();

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
        server.sendTextMessage("Hola desde el socket");
        vTaskDelay(pdMS_TO_TICKS(5));
        uint8_t pene[] = {0x01, 0x02, 0x03};
        server.sendBinaryMessage(pene, sizeof(pene));
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/**
 * @brief Task that monitors the free heap memory.
 *
 * This task logs the free and minimum free heap memory every second.
 *
 * @param pvp Task parameter (not used).
 */
void monitorTask(void *pvp)
{
    for (;;)
    {
        ESP_LOGI("MonitorTask", "Free heap: %lu", esp_get_free_heap_size());
        ESP_LOGI("MonitorTask", "Minimum free heap: %lu", esp_get_minimum_free_heap_size());

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


extern "C" void app_main(void)
{
    WSLightServer &server = WSLightServer::getInstance();

    server.onTextMessage([](int client_sock, const std::string &message)
                         { ESP_LOGI("WSLightServer", "Received text message: %s", message.c_str()); });

    server.onBinaryMessage([](int client_sock, const std::vector<uint8_t> &message)
                           {
                               ESP_LOGI("WSLightServer", "Received binary message");

                               esp_log_buffer_hex("WSLightServer", message.data(), message.size()); });

    server.onPingMessage([](int client_sock)
                         { ESP_LOGI("WSLightServer", "Received ping from client %d repliying with pong", client_sock); });

    server.onPongMessage([](int client_sock)
                         { ESP_LOGI("WSLightServer", "Received pong from client %d", client_sock); });

    server.onCloseMessage([](int client_sock)
                          { ESP_LOGI("WSLightServer", "Client %d closed connection", client_sock); });

    server.onClientConnected([](int client_sock)
                             { ESP_LOGI("WSLightServer", "Client connected: %d", client_sock); });

    server.onClientDisconnected([](int client_sock)
                                { ESP_LOGI("WSLightServer", "Client disconnected: %d", client_sock); });

    server.start("default_ssid", "default_password", 8080, 8000, 60000, false, []()
                 { ESP_LOGW("WSLightServer", "Test extra config"); });

    xTaskCreate(&nepe, "nepe", 4096, nullptr, tskIDLE_PRIORITY, nullptr);
    xTaskCreate(&mathTask, "mathTask", 6048, nullptr, tskIDLE_PRIORITY, nullptr);
    xTaskCreate(&arrayTask, "arrayTask", 10024, nullptr, tskIDLE_PRIORITY, nullptr);
    xTaskCreate(&compressionTask, "compressionTask", 6048, nullptr, tskIDLE_PRIORITY, nullptr);
    xTaskCreate(&monitorTask, "monitorTask", 4096, nullptr, tskIDLE_PRIORITY, nullptr);
}
