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
    bool d=false;
    bool e=false;
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
    bool raw=false;
    bool debounced=false;
    TickType_t rawChangeTick=0;
    bool idleKnown=false;
    bool idleLevel=false;
    bool pressArmed=false;
    bool toggled=false;
    bool toggledChanged=false;
};
class btnMgr{
    public:
        btnMgr(){btn.press.prev=false; btn.press.curr=false; btn.raw=false; btn.debounced=false; btn.rawChangeTick=0; btn.idleKnown=false; btn.idleLevel=false; btn.pressArmed=false; btn.toggled=false; debounceTicks=pdMS_TO_TICKS(kDefaultDebounceMs);};
        void set(pinManager& pin, const char* name){
            this->pin = &pin;
            this->name = name;
        }
    void setDebounceMs(uint32_t debounceMs){
        debounceTicks = pdMS_TO_TICKS(debounceMs);
    }
    uint32_t debounceMs() const {
        return static_cast<uint32_t>(pdTICKS_TO_MS(debounceTicks));
    }
    void update(){
        if (pin == nullptr || name == nullptr) {
            return;
        }

        btn.toggledChanged = false;

        const TickType_t nowTick = xTaskGetTickCount();
        const bool rawRead = pin->digitalRead(name);

        // Debounce raw transitions to avoid false release edges from contact bounce.
        if (rawRead != btn.raw) {
            btn.raw = rawRead;
            btn.rawChangeTick = nowTick;
        }

        if ((nowTick - btn.rawChangeTick) < debounceTicks || btn.debounced == btn.raw) {
            return;
        }

        btn.press.prev = btn.press.curr;
        btn.debounced = btn.raw;
        btn.press.curr = btn.debounced;

        // Learn idle level once, then toggle only on a full away-from-idle and back-to-idle cycle.
        if (!btn.idleKnown) {
            btn.idleLevel = btn.press.curr;
            btn.idleKnown = true;
            btn.pressArmed = false;
            return;
        }

        if (!btn.pressArmed && (btn.press.curr != btn.idleLevel)) {
            btn.pressArmed = true;
        } else if (btn.pressArmed && (btn.press.curr == btn.idleLevel)) {
            btn.toggled = !btn.toggled;
            btn.toggledChanged = true;
            btn.pressArmed = false;
        }
    }
    bool toggledState(){return btn.toggled;}
    void setToggledState(bool value){
        btn.toggled = value;
        btn.toggledChanged = false;
    }
    bool toggledChanged(){return btn.toggledChanged;}
    bool state(){return btn.raw;}
private:
    static constexpr uint32_t kDefaultDebounceMs = 25;
    TickType_t debounceTicks = 0;
    pinManager* pin = nullptr;
    btnStat btn;
    const char* name = nullptr;
};