idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES components
                             human_face_detect
                             human_face_recognition
                             esp-dl
                             esp_timer
                             spiffs
                             vfs
                             esp_driver_gpio
                             esp_wifi
                             esp_netif
                             esp_event
                             nvs_flash
                             esp_http_client)
