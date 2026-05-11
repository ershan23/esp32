set(src_dirs
            GPTIM
            LCD
            SPI
            LED)

set(include_dirs
            GPTIM
            LCD
            SPI
            LED)

set(requires
            esp_driver_gpio esp_driver_spi
            esp_driver_gptimer)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)