idf_component_register(
    SRCS "src/ws_light_server.cpp" 
    INCLUDE_DIRS "include"
    REQUIRES  freertos nvs_flash esp_timer  mbedtls esp_event esp_wifi
)
