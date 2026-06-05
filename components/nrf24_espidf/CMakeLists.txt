idf_component_register(SRCS "src/hal_espidf.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_spi esp_driver_gpio nrf24l01plus)
