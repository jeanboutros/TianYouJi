idf_component_register(SRCS "main.cpp"
                       PRIV_REQUIRES spi_flash esp_driver_spi esp_driver_gpio nrf24l01plus
                       INCLUDE_DIRS "")

# Suppress missing-field-initializers noise from C SDK structs used in C++ code
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
