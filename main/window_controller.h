#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    WINDOW_DIRECTION_CLOSE = -1,
    WINDOW_DIRECTION_STOP = 0,
    WINDOW_DIRECTION_OPEN = 1,
} window_direction_t;

typedef enum {
    WINDOW_BOOT_RELEASING,
    WINDOW_BOOT_HOMING,
    WINDOW_READY,
    WINDOW_BOOT_FAILED,
} window_boot_state_t;

typedef struct {
    int32_t encoder_count;
    int32_t full_travel_count;
    uint8_t position_percent;
    window_direction_t motion;
    window_direction_t blocked_direction;
    window_boot_state_t boot_state;
    bool calibrated;
    bool slip_active;
} window_status_t;

typedef void (*window_status_callback_t)(const window_status_t *status);

esp_err_t window_controller_init(window_status_callback_t callback);
void window_controller_open(void);
void window_controller_close(void);
void window_controller_stop(void);
void window_controller_set_position(uint8_t percent);
void window_controller_factory_reset(void);
window_status_t window_controller_get_status(void);
