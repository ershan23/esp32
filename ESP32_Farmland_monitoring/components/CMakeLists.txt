set(src_dirs
            CAMERA
            LCD
            SPI
            SENSOR
            ML
            ML/generated
            LED)

set(include_dirs
            CAMERA
            LCD
            SPI
            SENSOR
            ML
            ML/generated
            LED)

set(requires
            esp32-camera
            driver
            esp_driver_gpio
            esp_driver_i2c
            esp_driver_spi
            esp_driver_ledc
            esp_rom
            esp_timer
            espressif__esp-tflite-micro)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
