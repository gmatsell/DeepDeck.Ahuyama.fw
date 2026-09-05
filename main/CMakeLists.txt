idf_component_register(SRCS  "spiffs.c" "main.c" "deepdeck_tasks.c" "keymap.c" "matrix.c" "keypress_handles.c" "gesture_handles.c" "server.c" "wifi_handles.c"
                    INCLUDE_DIRS "."
                    REQUIRES "spiffs" "mdns" "esp_http_server" "battery" "cjson" "ble" "oled" "nvsfuncs" "plugins" "rotary_encoder" "rgb_led" "menu" "bus" "apds9960" "uuid_master" "esp_driver_gpio" "esp_adc" "esp_timer" "esp_wifi" "esp_netif" "esp_event" "espnowmk32") 


                    spiffs_create_partition_image(www_0 ../spiffs_image FLASH_IN_PROJECT)
