#include "ws2812.h"
#include "driver/rmt_tx.h"

#define RES_HZ 10000000                       /* 1 tick＝0.1µs */

static rmt_channel_handle_t s_ch;
static rmt_encoder_handle_t s_enc;
static uint32_t s_last = 0xFFFFFFFF;
static fa_led_order_t s_order;

esp_err_t ws2812_init(int gpio, fa_led_order_t order)
{
    s_order = order;
    rmt_tx_channel_config_t cc = {.gpio_num = gpio, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = RES_HZ,
                                  .mem_block_symbols = 48, .trans_queue_depth = 2};
    esp_err_t err = rmt_new_tx_channel(&cc, &s_ch);
    if (err != ESP_OK) return err;
    rmt_bytes_encoder_config_t ec = {                       /* WS2812：0＝0.3µs 高＋0.9µs 低；1＝0.9µs 高＋0.3µs 低；MSB 先 */
        .bit0 = {.level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9},
        .bit1 = {.level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3},
        .flags.msb_first = 1,
    };
    if ((err = rmt_new_bytes_encoder(&ec, &s_enc)) != ESP_OK) return err;
    return rmt_enable(s_ch);
}

esp_err_t ws2812_set(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t v = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
    if (!s_ch || v == s_last) return ESP_OK;
    static uint8_t wire[3];                                 /* 傳送完成前 buffer 要一直有效 */
    rmt_tx_wait_all_done(s_ch, 10);                         /* 上一筆送完（還要 ≥50µs 低電位才鎖存：呼叫間隔 10ms 足夠） */
    fa_led_wire_bytes((fa_rgb_t){r, g, b}, s_order, wire);
    rmt_transmit_config_t tc = {.loop_count = 0};
    esp_err_t err = rmt_transmit(s_ch, s_enc, wire, sizeof wire, &tc);
    if (err == ESP_OK) s_last = v;
    return err;
}
