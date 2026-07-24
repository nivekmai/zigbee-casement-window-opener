#include "window_controller.h"
#include "encoder.h"
#include "pins.h"

#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "window";
static const char *NVS_NS = "window";
static const char *NVS_TRAVEL = "travel";

#define INPUT_DEBOUNCE_MS          20
#define CALIBRATION_HOLD_MS      2000
#define FACTORY_RESET_HOLD_MS   10000
#define BOOT_RELEASE_TIMEOUT_MS  3000
#define HOMING_TIMEOUT_MS       45000
#define MOVEMENT_TIMEOUT_MS     45000
#define TARGET_TOLERANCE_COUNTS     3
#define ENDPOINT_ACCEPTANCE_PERCENT 5
#define CONTROL_PERIOD_MS           10
#define POSITION_REPORT_PERIOD_MS  250

typedef struct {
    window_direction_t motion;
    window_direction_t blocked_direction;
    window_boot_state_t boot_state;
    int32_t full_travel;
    int32_t target_count;
    bool target_active;
    bool manual_active;
    bool slip_active;
    bool open_button;
    bool close_button;
    uint32_t state_started_ms;
    uint32_t movement_started_ms;
    uint32_t both_started_ms;
    uint32_t last_report_ms;
    bool calibration_done_this_hold;
    window_status_callback_t callback;
} controller_t;

static controller_t s;

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

static bool input_active(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

static void motor_stop_hw(void)
{
    /* DRV8871: IN1=0 and IN2=0 -> outputs disabled/coast, then sleep. */
    gpio_set_level(PIN_MOTOR_IN1, 0);
    gpio_set_level(PIN_MOTOR_IN2, 0);
}

static void motor_drive_hw(window_direction_t direction)
{
    bool forward = (direction == WINDOW_DIRECTION_OPEN);
#if MOTOR_DIRECTION_INVERTED
    forward = !forward;
#endif
    gpio_set_level(PIN_MOTOR_IN1, forward ? 1 : 0);
    gpio_set_level(PIN_MOTOR_IN2, forward ? 0 : 1);
}

static uint8_t position_percent(void)
{
    if (s.full_travel <= 0) return 0;
    int64_t p = ((int64_t) encoder_get_count() * 100LL) / s.full_travel;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return (uint8_t) p;
}

static void notify(void)
{
    if (!s.callback) return;
    window_status_t st = {
        .encoder_count = encoder_get_count(),
        .full_travel_count = s.full_travel,
        .position_percent = position_percent(),
        .motion = s.motion,
        .blocked_direction = s.blocked_direction,
        .boot_state = s.boot_state,
        .calibrated = s.full_travel > 0,
        .slip_active = s.slip_active,
    };
    s.callback(&st);
}

static void stop_motion(void)
{
    motor_stop_hw();
    s.motion = WINDOW_DIRECTION_STOP;
    s.target_active = false;
    s.manual_active = false;
    notify();
}

static bool direction_allowed(window_direction_t direction)
{
    if (s.blocked_direction == WINDOW_DIRECTION_STOP) return true;
    return direction == -s.blocked_direction;
}

static void start_motion(window_direction_t direction, bool target, bool manual)
{
    if (s.boot_state == WINDOW_READY && !direction_allowed(direction)) {
        ESP_LOGW(TAG, "Direction %d blocked until the slip switch releases", direction);
        return;
    }
    motor_drive_hw(direction);
    s.motion = direction;
    s.target_active = target;
    s.manual_active = manual;
    s.movement_started_ms = now_ms();
    notify();
}

static void save_travel(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i32(nvs, NVS_TRAVEL, s.full_travel);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void load_travel(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_i32(nvs, NVS_TRAVEL, &s.full_travel) != ESP_OK) {
            s.full_travel = 0;
        }
        nvs_close(nvs);
    }
}

static void calibrate_open(void)
{
    if (s.slip_active) {
        ESP_LOGW(TAG, "Calibration rejected: slip switch active");
        return;
    }
    int32_t count = encoder_get_count();
    if (count < 10) {
        ESP_LOGW(TAG, "Calibration rejected: count %ld is too small", (long) count);
        return;
    }
    s.full_travel = count;
    save_travel();
    ESP_LOGI(TAG, "Saved full-open travel: %ld counts", (long) count);
    notify();
}

static void accept_closed(void)
{
    stop_motion();
    encoder_set_count(0);
    s.blocked_direction = WINDOW_DIRECTION_CLOSE;
    s.boot_state = WINDOW_READY;
    ESP_LOGI(TAG, "Closed reference accepted and encoder zeroed");
    notify();
}

static void handle_slip_trip(void)
{
    window_direction_t tripped = s.motion;
    ESP_LOGW(TAG, "Slip switch active while direction=%d; motor off", tripped);
    stop_motion();
    s.blocked_direction = tripped;

    if (s.boot_state == WINDOW_BOOT_HOMING &&
        tripped == WINDOW_DIRECTION_CLOSE) {
        accept_closed();
        return;
    }

    if (s.full_travel > 0 && !s.manual_active) {
        int32_t count = encoder_get_count();
        int32_t band = (s.full_travel * ENDPOINT_ACCEPTANCE_PERCENT) / 100;
        if (band < TARGET_TOLERANCE_COUNTS) band = TARGET_TOLERANCE_COUNTS;

        if (tripped == WINDOW_DIRECTION_CLOSE && count <= band) {
            encoder_set_count(0);
            ESP_LOGI(TAG, "Trip accepted as expected closed endpoint");
        } else if (tripped == WINDOW_DIRECTION_OPEN &&
                   count >= s.full_travel - band) {
            ESP_LOGI(TAG, "Trip accepted as expected open endpoint");
        } else {
            ESP_LOGW(TAG, "Trip treated as obstruction/unknown resistance");
        }
    }
    notify();
}

static void begin_boot(void)
{
    s.blocked_direction = WINDOW_DIRECTION_STOP;
    s.state_started_ms = now_ms();
    s.slip_active = input_active(PIN_SLIP_SWITCH);

    if (s.slip_active) {
        s.boot_state = WINDOW_BOOT_RELEASING;
        ESP_LOGI(TAG, "Boot: switch active, moving OPEN until release");
        start_motion(WINDOW_DIRECTION_OPEN, false, false);
    } else {
        s.boot_state = WINDOW_BOOT_HOMING;
        ESP_LOGI(TAG, "Boot: moving CLOSE until switch trip");
        start_motion(WINDOW_DIRECTION_CLOSE, false, false);
    }
}

static void controller_task(void *arg)
{
    bool previous_slip = input_active(PIN_SLIP_SWITCH);
    bool previous_open = false;
    bool previous_close = false;

    while (true) {
        uint32_t now = now_ms();
        bool slip = input_active(PIN_SLIP_SWITCH);
        bool open = input_active(PIN_BUTTON_OPEN);
        bool close = input_active(PIN_BUTTON_CLOSE);

        s.slip_active = slip;
        s.open_button = open;
        s.close_button = close;

        /* Rising edge of the mechanical safety switch always removes power. */
        if (slip && !previous_slip && s.motion != WINDOW_DIRECTION_STOP) {
            handle_slip_trip();
        }

        /* Backing away clears the directional latch when the mechanism re-engages. */
        if (!slip && previous_slip) {
            s.blocked_direction = WINDOW_DIRECTION_STOP;
            if (s.boot_state == WINDOW_BOOT_RELEASING) {
                stop_motion();
                s.boot_state = WINDOW_BOOT_HOMING;
                s.state_started_ms = now;
                start_motion(WINDOW_DIRECTION_CLOSE, false, false);
            }
            notify();
        }

        previous_slip = slip;

        if (s.boot_state == WINDOW_BOOT_RELEASING &&
            now - s.state_started_ms > BOOT_RELEASE_TIMEOUT_MS) {
            ESP_LOGE(TAG, "Boot release timeout");
            stop_motion();
            s.boot_state = WINDOW_BOOT_FAILED;
        }

        if (s.boot_state == WINDOW_BOOT_HOMING &&
            now - s.state_started_ms > HOMING_TIMEOUT_MS) {
            ESP_LOGE(TAG, "Boot homing timeout");
            stop_motion();
            s.boot_state = WINDOW_BOOT_FAILED;
        }

        if (s.boot_state == WINDOW_READY) {
            bool both = open && close;

            if (both) {
                if (s.motion != WINDOW_DIRECTION_STOP) stop_motion();
                if (s.both_started_ms == 0) s.both_started_ms = now;

                uint32_t held = now - s.both_started_ms;
                if (!s.calibration_done_this_hold &&
                    held >= CALIBRATION_HOLD_MS &&
                    held < FACTORY_RESET_HOLD_MS) {
                    calibrate_open();
                    s.calibration_done_this_hold = true;
                }
                if (held >= FACTORY_RESET_HOLD_MS) {
                    window_controller_factory_reset();
                    s.both_started_ms = now; /* avoid repeated resets */
                }
            } else {
                s.both_started_ms = 0;
                s.calibration_done_this_hold = false;

                if (open && !close) {
                    if (s.motion != WINDOW_DIRECTION_OPEN || !s.manual_active)
                        start_motion(WINDOW_DIRECTION_OPEN, false, true);
                } else if (close && !open) {
                    if (s.motion != WINDOW_DIRECTION_CLOSE || !s.manual_active)
                        start_motion(WINDOW_DIRECTION_CLOSE, false, true);
                } else if (s.manual_active && !open && !close) {
                    stop_motion();
                }
            }

            if (s.motion != WINDOW_DIRECTION_STOP &&
                now - s.movement_started_ms > MOVEMENT_TIMEOUT_MS) {
                ESP_LOGE(TAG, "Movement watchdog timeout");
                stop_motion();
            }

            if (s.target_active) {
                int32_t count = encoder_get_count();
                if ((s.motion == WINDOW_DIRECTION_OPEN &&
                     count >= s.target_count - TARGET_TOLERANCE_COUNTS) ||
                    (s.motion == WINDOW_DIRECTION_CLOSE &&
                     count <= s.target_count + TARGET_TOLERANCE_COUNTS)) {
                    stop_motion();
                }
            }

            if (now - s.last_report_ms >= POSITION_REPORT_PERIOD_MS) {
                s.last_report_ms = now;
                notify();
            }
        }

        previous_open = open;
        previous_close = close;
        (void) previous_open;
        (void) previous_close;
        vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

esp_err_t window_controller_init(window_status_callback_t callback)
{
    memset(&s, 0, sizeof(s));
    s.callback = callback;

    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_IN1) | (1ULL << PIN_MOTOR_IN2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "motor GPIO");
    motor_stop_hw();

    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << PIN_BUTTON_OPEN) |
                        (1ULL << PIN_BUTTON_CLOSE) |
                        (1ULL << PIN_SLIP_SWITCH),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "input GPIO");
    ESP_RETURN_ON_ERROR(encoder_init(), TAG, "encoder");

    load_travel();
    begin_boot();
    xTaskCreate(controller_task, "window_ctrl", 4096, NULL, 10, NULL);
    return ESP_OK;
}

void window_controller_open(void)
{
    if (s.boot_state == WINDOW_READY)
        start_motion(WINDOW_DIRECTION_OPEN, false, false);
}

void window_controller_close(void)
{
    if (s.boot_state == WINDOW_READY)
        start_motion(WINDOW_DIRECTION_CLOSE, false, false);
}

void window_controller_stop(void)
{
    stop_motion();
}

void window_controller_set_position(uint8_t percent)
{
    if (s.boot_state != WINDOW_READY || s.full_travel <= 0) {
        ESP_LOGW(TAG, "Set-position rejected: not ready or not calibrated");
        return;
    }
    if (percent > 100) percent = 100;

    s.target_count = (int32_t) (((int64_t) s.full_travel * percent) / 100);
    int32_t current = encoder_get_count();
    if (abs(s.target_count - current) <= TARGET_TOLERANCE_COUNTS) {
        stop_motion();
    } else if (s.target_count > current) {
        start_motion(WINDOW_DIRECTION_OPEN, true, false);
    } else {
        start_motion(WINDOW_DIRECTION_CLOSE, true, false);
    }
}

void window_controller_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset requested");
    stop_motion();
    nvs_flash_erase();
    esp_restart();
}

window_status_t window_controller_get_status(void)
{
    window_status_t st = {
        .encoder_count = encoder_get_count(),
        .full_travel_count = s.full_travel,
        .position_percent = position_percent(),
        .motion = s.motion,
        .blocked_direction = s.blocked_direction,
        .boot_state = s.boot_state,
        .calibrated = s.full_travel > 0,
        .slip_active = s.slip_active,
    };
    return st;
}
