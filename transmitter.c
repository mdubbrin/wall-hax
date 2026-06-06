#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#define TAG "CSI-TX"

//network configuration 
#define WIFI_SSID "ssid"
#define WIFI_PASS "password"
#define WIFI_CHANNEL 6
#define TX_RATE_MS 10 //100 packets/sec

//Minimal null data frame (broadcast)
static const uint8_t null_frame[] = {
    0x48, 0x00,             //frame control: null data
    0x00, 0x00,             //duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //dst: broadcast
    0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, //src: fake mac
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //bssid: broadcast
    0x00, 0x00 //seq
};

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        ESP_LOGI(TAG, "connected");
}

static void transmit(void *arg)
{
    while(1){
        esp_wifi_80211_tx(WIFI_IF_STA, null_frame, sizeof(null_frame), false);
        vTaskDelay(pdMS_TO_TICKS(TX_RATE_MS));
    }
}

void app_main(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wifi_cfg = {.sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        }
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_start();

    ESP_LOGI(TAG, "TX running @ %d Hz on ch%d", 1000 / TX_RATE_MS, WIFI_CHANNEL);
    xTaskCreate(transmit, "tx", 2048, NULL, 5, NULL);
}
