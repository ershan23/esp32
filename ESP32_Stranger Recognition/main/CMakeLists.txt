idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES components
                             human_face_detect
                             human_face_recognition
                             esp_timer
                             spiffs
                             vfs
                             esp_driver_gpio)
