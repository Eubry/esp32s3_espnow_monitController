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
struct sensorDta{
    bool a=false;
    bool b=false;
    bool c=false;
};
struct carDta{
    motDta motL;
    motDta motR;
    sensorDta sensor;
};
struct btnDta{
    uint8_t state = 0;
};
struct statBool{
    bool curr=false;
    bool prev=false;
};
struct btnStat{
    statBool press;
    bool state=false;
};
class btnMgr{
    public:
        btnMgr(){btn.press.prev=true; btn.press.curr=false; btn.state=false;};
        void set(pinManager& pin, const char* name){
            this->pin = &pin;
            this->name = name;
        }
    void update(){
        btn.press.curr = pin->digitalRead(name);
        if(btn.press.curr==false&&btn.press.curr!=btn.press.prev){
            btn.press.prev = btn.press.curr;
            btn.state=!btn.state;
        }else{
            btn.press.prev = btn.press.curr;
        }
    }
    bool state(){return btn.state;}
private:
    pinManager* pin = nullptr;
    btnStat btn;
    const char* name = nullptr;
};