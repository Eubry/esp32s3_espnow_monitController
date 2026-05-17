#include "main.h"
// -----Global variables---------------------------------
RGBStrip bLed;
Utils::taskManager taskMgr;
pinManager pin;
extern "C" void app_main(void){
    ESP_LOGI("APP_MAIN", "Starting app controller...");
    bLed.begin();// WS2812 onboard LED on many ESP32-S3 Supermini boards is on GPIO48.
    bLed.color(100, 0, 100);// Set initial color to purple to indicate startup
}