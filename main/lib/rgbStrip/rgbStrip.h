#include "led_strip.h"
#include "driver/rmt_tx.h"
#define WS2812_GPIO      48
#define WS2812_LED_COUNT 1
class RGBStrip{
    private:
        led_strip_handle_t strip;// Handle for the LED ws2812 strip
    public:
        RGBStrip(){};
        esp_err_t begin(){
            strip = NULL;
            led_strip_config_t strip_config = {
                .strip_gpio_num = WS2812_GPIO,
                .max_leds = WS2812_LED_COUNT,
                .led_model = LED_MODEL_WS2812,
                .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
                .flags = {
                    .invert_out = false,
                },
            };
            led_strip_rmt_config_t rmt_config = {
                .clk_src = RMT_CLK_SRC_DEFAULT,
                .resolution_hz = 10 * 1000 * 1000,
                .mem_block_symbols = 64,
                .flags = {
                    .with_dma = false,
                },
            };
            esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
            if (err != ESP_OK) {
                ESP_LOGE("APP_MAIN", "WS2812 init failed on GPIO %d: %s", WS2812_GPIO, esp_err_to_name(err));
                return err;
            }
            return ESP_OK;
        }
        void color(uint8_t red, uint8_t green, uint8_t blue){
            if(strip == NULL){
                ESP_LOGE("RGB_ESP32S3", "LED strip not initialized. Call begin() first.");
                return;
            }
            esp_err_t err = led_strip_set_pixel(strip, 0, red, green, blue);
            if (err == ESP_OK) {
                err = led_strip_refresh(strip);
            }
            if (err != ESP_OK) {
                ESP_LOGE("RGB_ESP32S3", "WS2812 set/refresh failed: %s", esp_err_to_name(err));
            }
        }
        ~RGBStrip(){
            if(strip != NULL){
                led_strip_del(strip);
            }
        }
};