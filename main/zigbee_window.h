#pragma once
#include "esp_err.h"
#include "window_controller.h"

esp_err_t zigbee_window_start(void);
void zigbee_window_publish_status(const window_status_t *status);
