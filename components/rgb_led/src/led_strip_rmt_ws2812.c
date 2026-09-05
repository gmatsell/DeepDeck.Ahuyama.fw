// WS2812 LED strip driver using the ESP-IDF 5.x/6.x RMT API.
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "led_strip.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

static const char *TAG = "ws2812";

#define STRIP_CHECK(a, str, goto_tag, ret_value, ...)                             \
    do                                                                            \
    {                                                                             \
        if (!(a))                                                                 \
        {                                                                         \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = ret_value;                                                      \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

// RMT resolution: 10 MHz → 100 ns per tick
#define RMT_RESOLUTION_HZ   10000000

// WS2812 bit timing (in ticks at 10 MHz):
#define WS2812_T0H_TICKS  3   // 300 ns
#define WS2812_T0L_TICKS  9   // 900 ns
#define WS2812_T1H_TICKS  7   // 700 ns
#define WS2812_T1L_TICKS  5   // 500 ns

typedef struct {
    led_strip_t parent;
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t encoder;
    uint32_t strip_len;
    uint8_t buffer[0];
} ws2812_t;

static esp_err_t ws2812_set_pixel(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue)
{
    esp_err_t ret = ESP_OK;
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    STRIP_CHECK(index < ws2812->strip_len, "index out of the maximum number of leds", err, ESP_ERR_INVALID_ARG);
    uint32_t start = index * 3;
    // WS2812 data order: GRB
    ws2812->buffer[start + 0] = green & 0xFF;
    ws2812->buffer[start + 1] = red & 0xFF;
    ws2812->buffer[start + 2] = blue & 0xFF;
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ws2812_refresh(led_strip_t *strip, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    STRIP_CHECK(rmt_transmit(ws2812->rmt_chan, ws2812->encoder,
                             ws2812->buffer, ws2812->strip_len * 3,
                             &tx_config) == ESP_OK,
                "transmit RMT samples failed", err, ESP_FAIL);
    STRIP_CHECK(rmt_tx_wait_all_done(ws2812->rmt_chan, (int)timeout_ms) == ESP_OK,
                "wait RMT done failed", err, ESP_FAIL);
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ws2812_clear(led_strip_t *strip, uint32_t timeout_ms)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    memset(ws2812->buffer, 0, ws2812->strip_len * 3);
    return ws2812_refresh(strip, timeout_ms);
}

static esp_err_t ws2812_del(led_strip_t *strip)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    rmt_del_encoder(ws2812->encoder);
    rmt_disable(ws2812->rmt_chan);
    rmt_del_channel(ws2812->rmt_chan);
    free(ws2812);
    return ESP_OK;
}

led_strip_t *led_strip_new_rmt_ws2812(const led_strip_config_t *config)
{
    led_strip_t *ret = NULL;
    STRIP_CHECK(config, "configuration can't be null", err, NULL);

    uint32_t ws2812_size = sizeof(ws2812_t) + config->max_leds * 3;
    ws2812_t *ws2812 = calloc(1, ws2812_size);
    STRIP_CHECK(ws2812, "request memory for ws2812 failed", err, NULL);

    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num        = config->gpio,
        .clk_src         = RMT_CLK_SRC_DEFAULT,
        .resolution_hz   = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .intr_priority   = 1,
    };
    if (rmt_new_tx_channel(&tx_chan_config, &ws2812->rmt_chan) != ESP_OK) {
        ESP_LOGE(TAG, "create RMT TX channel failed");
        free(ws2812);
        goto err;
    }

    rmt_bytes_encoder_config_t bytes_enc_config = {
        .bit0 = {
            .level0 = 1, .duration0 = WS2812_T0H_TICKS,
            .level1 = 0, .duration1 = WS2812_T0L_TICKS,
        },
        .bit1 = {
            .level0 = 1, .duration0 = WS2812_T1H_TICKS,
            .level1 = 0, .duration1 = WS2812_T1L_TICKS,
        },
        .flags.msb_first = 1,
    };
    if (rmt_new_bytes_encoder(&bytes_enc_config, &ws2812->encoder) != ESP_OK) {
        ESP_LOGE(TAG, "create bytes encoder failed");
        rmt_del_channel(ws2812->rmt_chan);
        free(ws2812);
        goto err;
    }

    rmt_enable(ws2812->rmt_chan);

    ws2812->strip_len = config->max_leds;
    ws2812->parent.set_pixel = ws2812_set_pixel;
    ws2812->parent.refresh = ws2812_refresh;
    ws2812->parent.clear = ws2812_clear;
    ws2812->parent.del = ws2812_del;

    return &ws2812->parent;
err:
    return ret;
}

led_strip_t *led_strip_init(uint8_t channel, uint8_t gpio, uint16_t led_num)
{
    led_strip_config_t strip_config = {
        .gpio    = gpio,
        .max_leds = led_num,
    };
    led_strip_t *pStrip = led_strip_new_rmt_ws2812(&strip_config);
    if (!pStrip) {
        ESP_LOGE(TAG, "install WS2812 driver failed");
        return NULL;
    }
    ESP_ERROR_CHECK(pStrip->clear(pStrip, 100));
    return pStrip;
}

esp_err_t led_strip_denit(led_strip_t *strip)
{
    return strip->del(strip);
}
