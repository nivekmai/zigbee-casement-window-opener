#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t encoder_init(void);
int32_t encoder_get_count(void);
void encoder_set_count(int32_t value);
