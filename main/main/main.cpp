#include "main.h"
// -----Global variables---------------------------------
RGBStrip bLed;
Utils::taskManager taskMgr;
pinManager pin;
OLEDDisplay dsp;
// ------------------------------------------------------
// -----Function prototypes------------------------------
static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
// ------------------------------------------------------
// -----Data structures----------------------------------
carDta car;
// ------------------------------------------------------
// -----Tasks--------------------------------------------
void dspTask(void* param);
// ------------------------------------------------------
extern "C" void app_main(void){
    ESP_LOGI("APP_MAIN", "Starting app controller...");
    bLed.begin();// WS2812 onboard LED on many ESP32-S3 Supermini boards is on GPIO48.
    bLed.color(100, 0, 100);// Set initial color to purple to indicate startup
    //------------------------------------------
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));

    ESP_LOGI("Receiver", "Receptor listo. Esperando mensajes...");
    // -----Task for displyaying data on OLED and controlling RGB LED based on received data-----
    taskMgr.add("DisplayTask",dspTask, NULL, 1, 0, 4096);
}
static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len == sizeof(carDta)) {
        memcpy(&car, data, sizeof(car));
        
        ESP_LOGI("Car data", "MAC: " MACSTR " -> Velocidad izquierda: %d, Dirección izquierda: %d, Velocidad derecha: %d, Dirección derecha: %d",
                 MAC2STR(recv_info->src_addr),
                 car.motL.speed, car.motL.dir, car.motR.speed, car.motR.dir);
    } else {
        ESP_LOGW("REC", "Paquete recibido con tamaño inesperado: %d", len);
    }
}
void dspTask(void* param){
    while(true){
        // Update OLED display with current car data
        dsp.clear();
        dsp.drawString(0, 0, "Car Data:");
        dsp.drawString(0, 10, "L Speed: " + std::to_string(car.motL.speed));
        dsp.drawString(0, 20, "L Dir: " + std::to_string(car.motL.dir));
        dsp.drawString(0, 30, "R Speed: " + std::to_string(car.motR.speed));
        dsp.drawString(0, 40, "R Dir: " + std::to_string(car.motR.dir));
        dsp.update();

        // Set RGB LED color based on motor speeds
        uint8_t red = (car.motL.speed > 0) ? 255 : 0;
        uint8_t green = (car.motR.speed > 0) ? 255 : 0;
        bLed.color(red, green, 0);

        vTaskDelay(pdMS_TO_TICKS(100)); // Update every 100ms
    }
}
/*
#include "oledDisplay.h"

OLEDDisplay display;

void setup() {
    if (display.init()) {
        display.clear();
        display.drawString(0, 0, "Hello!");
        display.drawRect(0, 10, 128, 20, false, true);
        display.update();
    }
}
*/