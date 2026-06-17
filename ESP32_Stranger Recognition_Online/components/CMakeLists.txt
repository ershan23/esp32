set(src_dirs
            CAMERA)

set(include_dirs
            CAMERA)

set(requires
            esp32-camera
            esp_driver_gpio)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format)
