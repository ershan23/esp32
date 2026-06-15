set(src_dirs
            CAMERA
            LCD
            SPI
            LED)

set(include_dirs
            CAMERA
            LCD
            SPI
            LED)

set(requires
            esp32-camera
            esp_driver_gpio
            esp_driver_ledc
            esp_driver_spi)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format)
