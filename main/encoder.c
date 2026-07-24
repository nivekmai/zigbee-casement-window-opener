#include "encoder.h"
#include "pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int32_t s_count;
static volatile uint8_t s_prev_ab;

/*
 * Compact quadrature transition table. Illegal/bounced transitions contribute 0.
 * Encoder is a passive 3-pin device:
 *   COMMON -> GND, A/B -> GPIO with pull-ups.
 */
static const int8_t QUAD_TABLE[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0
};

static void IRAM_ATTR encoder_isr(void *arg)
{
    uint8_t a = (uint8_t) gpio_get_level(PIN_ENCODER_A);
    uint8_t b = (uint8_t) gpio_get_level(PIN_ENCODER_B);
    uint8_t ab = (a << 1) | b;
    uint8_t index = (s_prev_ab << 2) | ab;
    int8_t delta = QUAD_TABLE[index & 0x0F];

    portENTER_CRITICAL_ISR(&s_mux);
    s_count += delta;
    s_prev_ab = ab;
    portEXIT_CRITICAL_ISR(&s_mux);
}

esp_err_t encoder_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_A) | (1ULL << PIN_ENCODER_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    s_prev_ab = ((uint8_t) gpio_get_level(PIN_ENCODER_A) << 1) |
                (uint8_t) gpio_get_level(PIN_ENCODER_B);

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ENCODER_A, encoder_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ENCODER_B, encoder_isr, NULL));
    return ESP_OK;
}

int32_t encoder_get_count(void)
{
    int32_t value;
    portENTER_CRITICAL(&s_mux);
    value = s_count;
    portEXIT_CRITICAL(&s_mux);
    return value;
}

void encoder_set_count(int32_t value)
{
    portENTER_CRITICAL(&s_mux);
    s_count = value;
    portEXIT_CRITICAL(&s_mux);
}
