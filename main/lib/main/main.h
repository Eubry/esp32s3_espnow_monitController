#pragma once
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_err.h"// Include ESP error codes
#include "esp_log.h"// Add ESP logging support
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "Utils.h"
#include "Counter.h"
#include "pinManager.h"
#include "pitchManager.h"
#include "rgbStrip.h"
#include "oledDisplay.h"

struct motDta{
    int16_t speed=0;
    int8_t dir=0;
};
struct carDta{
    motDta motL;
    motDta motR;
};