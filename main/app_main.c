#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_log.h"
#include "window_controller.h"
#include "zigbee_window.h"

static const char *TAG = "app";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /*
     * Motor GPIOs initialize OFF before Zigbee starts. Local operation,
     * homing and mechanical safety do not depend on the Zigbee network.
     */
    ESP_ERROR_CHECK(window_controller_init(zigbee_window_publish_status));
    ESP_ERROR_CHECK(zigbee_window_start());
    ESP_LOGI(TAG, "Casement window controller started");
}
