idf_component_register(
  SRCS
    "app_main.c"
    "window_controller.c"
    "encoder.c"
    "zigbee_window.c"
  INCLUDE_DIRS "."
  REQUIRES
    driver
    nvs_flash
    esp_timer
    esp_zigbee_lib
)
