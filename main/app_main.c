#include <stdio.h>
#include <string.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#include "driver/uart.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>


#include "qrcode.h"




static const char *LOG_TAG = "wifi_provisioning_example";
static EventGroupHandle_t wifi_event_group;

#define SERVICE_NAME_PREFIX "DEVICE_"

static void generate_service_name(char *name_buffer, size_t buffer_size) {
    uint8_t mac_addr[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac_addr);
    snprintf(name_buffer, buffer_size, "%s%02X%02X%02X",
             SERVICE_NAME_PREFIX, mac_addr[3], mac_addr[4], mac_addr[5]);
}

const int WIFI_CONNECTED_FLAG = BIT0;
static void system_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(LOG_TAG, "WiFI Provisioning Start");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_credentials = (wifi_sta_config_t *)event_data;
                ESP_LOGI(LOG_TAG, "Wi-Fi Credentials received:\n\tSSID: %s\n\tPassword: %s",
                         (const char *)wifi_credentials->ssid, (const char *)wifi_credentials->password);
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(LOG_TAG, "Success");
                break;
            case WIFI_PROV_CRED_FAIL:
                ESP_LOGE(LOG_TAG, "Failed");
                break;
            case WIFI_PROV_END:
                wifi_prov_mgr_deinit();
                ESP_LOGI(LOG_TAG, "Ended");
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(LOG_TAG, "Disconnected from Wi-Fi. Reconnecting...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(LOG_TAG, "Wi-Fi connected successfully");
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip_event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(LOG_TAG, "IP address: " IPSTR, IP2STR(&ip_event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_FLAG);
    }
}

static void display_qr_code(const char *service_name, const char *pop_key, const char *transport_type) {
    char provisioning_data[150] = {0};
    snprintf(provisioning_data, sizeof(provisioning_data), "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
             service_name, pop_key, transport_type);

    ESP_LOGI(LOG_TAG, "Scan the QR code below to start provisioning:");

    esp_qrcode_config_t qrcode_config = ESP_QRCODE_CONFIG_DEFAULT();

    esp_err_t result = esp_qrcode_generate(&qrcode_config, provisioning_data);
    if (result == ESP_OK) {
        ESP_LOGI(LOG_TAG, "QR Code successfully generated");
    } else {
        ESP_LOGE(LOG_TAG, "QR Code generation failed");
    }
}

void initialize_wifi_sta(void) {
    ESP_LOGI(LOG_TAG, "Configuring Wi-Fi in Station mode");
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void begin_ble_provisioning(void) {
    ESP_LOGI(LOG_TAG, "Initiating BLE provisioning");

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &system_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &system_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &system_event_handler, NULL));

    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    initialize_wifi_sta();

    wifi_prov_mgr_config_t provisioning_config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(provisioning_config));

    wifi_prov_security_t security_type = WIFI_PROV_SECURITY_1;
    const char *pop_key = "abcd1234";

    char service_name[12];
    generate_service_name(service_name, sizeof(service_name));

    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security_type, pop_key, service_name, NULL));

    display_qr_code(service_name, pop_key, "ble");

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_FLAG, false, true, portMAX_DELAY);

    ESP_LOGI(LOG_TAG, "Provisioning completed successfully");
}

void app_main(void) {
    uart_set_baudrate(UART_NUM_0, 115200);
    begin_ble_provisioning();
}