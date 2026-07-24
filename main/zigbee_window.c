/*
 * Zigbee adapter pinned to Espressif esp-zigbee-lib 1.6.8.
 *
 * Endpoint 1 exposes Basic, Identify and Window Covering (0x0102) server
 * clusters. Window Covering commands:
 *   0x00 Up/Open
 *   0x01 Down/Close
 *   0x02 Stop
 *   0x05 Go To Lift Percentage
 *
 * CurrentPositionLiftPercentage (0x0008) is updated from the encoder.
 */
#include "zigbee_window.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "esp_zigbee_cluster.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_ha_standard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "zigbee";
#define WINDOW_ENDPOINT 1
#define WINDOW_CLUSTER_ID 0x0102
#define ATTR_WINDOW_COVERING_TYPE             0x0000
#define ATTR_CURRENT_POSITION_LIFT_PERCENTAGE 0x0008
#define ATTR_CONFIG_STATUS                    0x0007
#define ATTR_OPERATIONAL_STATUS               0x000A
#define ATTR_MODE                             0x0017

#define CMD_UP_OPEN                0x00
#define CMD_DOWN_CLOSE             0x01
#define CMD_STOP                   0x02
#define CMD_GO_TO_LIFT_PERCENTAGE  0x05

/* ZCL roller-shade / lift-only values. */
static uint8_t s_covering_type = 0x00;
static uint8_t s_position = 0;
static uint8_t s_config_status = 0x03; /* operational + online */
static uint8_t s_operational_status = 0;
static uint8_t s_mode = 0;

static esp_zb_cluster_list_t *create_clusters(void)
{
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();

    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    esp_zb_attribute_list_t *basic =
        esp_zb_basic_cluster_create(&basic_cfg);
    uint8_t manufacturer[] = {8, 'O','p','e','n','A','I',' ',' '};
    uint8_t model[] = {16, 'C','a','s','e','m','e','n','t','O','p','e','n','e','r','C','6'};
    esp_zb_basic_cluster_add_attr(basic,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer);
    esp_zb_basic_cluster_add_attr(basic,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model);
    esp_zb_cluster_list_add_basic_cluster(
        clusters, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_identify_cluster_cfg_t identify_cfg = {.identify_time = 0};
    esp_zb_attribute_list_t *identify =
        esp_zb_identify_cluster_create(&identify_cfg);
    esp_zb_cluster_list_add_identify_cluster(
        clusters, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /*
     * 1.6.x has no public convenience constructor on every point release, so
     * build the standard cluster with the public generic attribute-list API.
     */
    esp_zb_attribute_list_t *wc =
        esp_zb_zcl_attr_list_create(WINDOW_CLUSTER_ID);

    esp_zb_cluster_add_attr(wc, WINDOW_CLUSTER_ID,
        ATTR_WINDOW_COVERING_TYPE, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_covering_type);
    esp_zb_cluster_add_attr(wc, WINDOW_CLUSTER_ID,
        ATTR_CONFIG_STATUS, ESP_ZB_ZCL_ATTR_TYPE_8BITMAP,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_config_status);
    esp_zb_cluster_add_attr(wc, WINDOW_CLUSTER_ID,
        ATTR_CURRENT_POSITION_LIFT_PERCENTAGE, ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        &s_position);
    esp_zb_cluster_add_attr(wc, WINDOW_CLUSTER_ID,
        ATTR_OPERATIONAL_STATUS, ESP_ZB_ZCL_ATTR_TYPE_8BITMAP,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        &s_operational_status);
    esp_zb_cluster_add_attr(wc, WINDOW_CLUSTER_ID,
        ATTR_MODE, ESP_ZB_ZCL_ATTR_TYPE_8BITMAP,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_mode);

    esp_zb_cluster_list_add_custom_cluster(
        clusters, wc, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    return clusters;
}

static esp_err_t command_handler(
    const esp_zb_zcl_custom_cluster_command_message_t *message)
{
    if (!message || message->info.cluster != WINDOW_CLUSTER_ID ||
        message->info.dst_endpoint != WINDOW_ENDPOINT) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Window command 0x%02x, payload=%u",
             message->info.command.id, (unsigned) message->data.size);

    switch (message->info.command.id) {
        case CMD_UP_OPEN:
            window_controller_open();
            break;
        case CMD_DOWN_CLOSE:
            window_controller_close();
            break;
        case CMD_STOP:
            window_controller_stop();
            break;
        case CMD_GO_TO_LIFT_PERCENTAGE:
            if (message->data.size >= 1 && message->data.value) {
                uint8_t percent = *((const uint8_t *) message->data.value);
                window_controller_set_position(percent);
            } else {
                ESP_LOGW(TAG, "Go-to-percentage command had no payload");
            }
            break;
        default:
            ESP_LOGW(TAG, "Unsupported Window Covering command 0x%02x",
                     message->info.command.id);
            break;
    }
    return ESP_OK;
}

static esp_err_t action_handler(esp_zb_core_action_callback_id_t callback_id,
                                const void *message)
{
    switch (callback_id) {
        case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID:
            return command_handler(
                (const esp_zb_zcl_custom_cluster_command_message_t *) message);
        default:
            return ESP_OK;
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *signal_p = signal_struct->p_app_signal;
    esp_zb_app_signal_type_t sig_type = *signal_p;
    esp_err_t status = signal_struct->esp_err_status;

    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Zigbee stack initialized");
            esp_zb_bdb_start_top_level_commissioning(
                ESP_ZB_BDB_MODE_INITIALIZATION);
            break;

        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (status == ESP_OK) {
                if (esp_zb_bdb_is_factory_new()) {
                    ESP_LOGI(TAG, "Starting network steering");
                    esp_zb_bdb_start_top_level_commissioning(
                        ESP_ZB_BDB_MODE_NETWORK_STEERING);
                } else {
                    ESP_LOGI(TAG, "Rejoined Zigbee network");
                }
            }
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (status == ESP_OK) {
                ESP_LOGI(TAG, "Joined Zigbee network");
            } else {
                ESP_LOGW(TAG, "Steering failed; retrying");
                esp_zb_scheduler_alarm(
                    (esp_zb_callback_t)
                    esp_zb_bdb_start_top_level_commissioning,
                    ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
            }
            break;

        default:
            ESP_LOGD(TAG, "Zigbee signal %s status=%s",
                     esp_zb_zdo_signal_to_string(sig_type),
                     esp_err_to_name(status));
            break;
    }
}

static void zigbee_task(void *arg)
{
    esp_zb_cfg_t cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&cfg);

    esp_zb_ep_list_t *endpoints = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = WINDOW_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(endpoints, create_clusters(), ep_cfg);
    esp_zb_device_register(endpoints);
    esp_zb_core_action_handler_register(action_handler);

    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

esp_err_t zigbee_window_start(void)
{
    esp_zb_platform_config_t platform = {
        .radio_config = {
            .radio_mode = ESP_ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ESP_ZB_HOST_CONNECTION_MODE_NONE,
        },
    };
    ESP_RETURN_ON_ERROR(
        esp_zb_platform_config(&platform), TAG, "platform config");

    xTaskCreate(zigbee_task, "zigbee", 8192, NULL, 5, NULL);
    return ESP_OK;
}

void zigbee_window_publish_status(const window_status_t *status)
{
    if (!status) return;

    s_position = status->position_percent;
    if (status->motion == WINDOW_DIRECTION_OPEN) {
        s_operational_status = 0x01;
    } else if (status->motion == WINDOW_DIRECTION_CLOSE) {
        s_operational_status = 0x02;
    } else {
        s_operational_status = 0x00;
    }

    /*
     * Zigbee stack data-model calls must be protected by the Zigbee lock.
     * Updating the attribute drives configured reporting to the coordinator.
     */
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(
        WINDOW_ENDPOINT,
        WINDOW_CLUSTER_ID,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ATTR_CURRENT_POSITION_LIFT_PERCENTAGE,
        &s_position,
        false);
    esp_zb_zcl_set_attribute_val(
        WINDOW_ENDPOINT,
        WINDOW_CLUSTER_ID,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ATTR_OPERATIONAL_STATUS,
        &s_operational_status,
        false);
    esp_zb_lock_release();
}
