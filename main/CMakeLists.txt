idf_component_register(SRCS "main.cpp" "hal_espidf.cpp"
                       PRIV_REQUIRES esp_driver_spi esp_driver_gpio nrf24l01plus
                       INCLUDE_DIRS "")

# Suppress missing-field-initializers noise from C SDK structs used in C++ code
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
