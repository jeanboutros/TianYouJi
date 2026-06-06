idf_component_register(SRCS "main.cpp"
                       PRIV_REQUIRES esp_driver_gpio esp_hw_support esp_timer nrf24l01plus nrf24_espidf
                       INCLUDE_DIRS "")

# Suppress missing-field-initializers noise from C SDK structs used in C++ code
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
