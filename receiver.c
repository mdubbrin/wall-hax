#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

#define TAG "CSI-RX"

#define WIFI_SSID "ssid"
#define WIFI_PASS "password"
#define WIFI_CHANNEL 6

#define PI_IP "x.x.x.x"
#define PI_PORT 5005

#define CSI_SUBCARRIERS 52
#define FRAME_BYTES (CSI_SUBCARRIERS * 2 * sizeof(int8_t) + 4)

static RingbufHandle_t rb;
static int udp_sock = -1;
static struct sockaddr_in pi_addr;
static uint32_t seq = 0;

typedef struct {
    uint32_t seq;
    int8_t payload[CSI_SUBCARRIERS * 2]; // interleaved I, Q per subcarrier
} __attribute__((packed)) csi_frame_t;

static void wifi_csi_cb(void *ctx, wifi_csi_info_t *info)
{
    if(!info || !info->buf) return;
    if(info->len < CSI_SUBCARRIERS * 2) return;

    csi_frame_t frame;
    frame.seq = seq++;
    memcpy(frame.payload, info->buf, CSI_SUBCARRIERS * 2);

    xRingbufferSend(rb, &frame, sizeof(frame), 0);
}

static void udp_tx_task(void *arg)
{
    size_t sz;
    csi_frame_t *frame;

    while(1){
        frame = xRingbufferReceive(rb, &sz, portMAX_DELAY);
        if(!frame) continue;

        sendto(udp_sock, frame, sizeof(csi_frame_t), 0,
               (struct sockaddr *)&pi_addr, sizeof(pi_addr));

        vRingbufferReturnItem(rb, frame);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP){
        ESP_LOGI(TAG, "IP acquired, starting UDP stream -> %s:%d", PI_IP, PI_PORT);

        udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        memset(&pi_addr, 0, sizeof(pi_addr));
        pi_addr.sin_family      = AF_INET;
        pi_addr.sin_port        = htons(PI_PORT);
        inet_pton(AF_INET, PI_IP, &pi_addr.sin_addr);

        xTaskCreatePinnedToCore(udp_tx_task, "udp_tx", 4096, NULL, 5, NULL, 1);
    }
}

void app_main(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    rb = xRingbufferCreate(4096, RINGBUF_TYPE_NOSPLIT);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
        }
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_start();

    // CSI config: enable LLTF (legacy long training field), 52 subcarriers
    wifi_csi_config_t csi_cfg = {
        .lltf_en = true,
        .htltf_en = false,
        .stbc_htltf2_en = false,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
    };
    esp_wifi_set_csi_config(&csi_cfg);
    esp_wifi_set_csi_rx_cb(wifi_csi_cb, NULL);
    esp_wifi_set_csi(true);

    ESP_LOGI(TAG, "CSI RX ready, waiting for IP...");
}
