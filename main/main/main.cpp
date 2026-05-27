#include "main.h"
// -----Global variables---------------------------------
RGBStrip bLed;
Utils::taskManager taskMgr;
pinManager pin;
OLEDDisplay dsp(GPIO_NUM_8, GPIO_NUM_9, 0x3C);  // SDA=8, SCL=9, Address=0x3C
static bool s_displayReady = false;
btnMgr btnAct;
static uint8_t s_peerMac[ESP_NOW_ETH_ALEN] = {0};
static bool s_peerKnown = false;
static bool s_lastBtnState = false;
static bool s_buttonStateSynced = false;
//static const uint8_t kSensorSrcMac[ESP_NOW_ETH_ALEN] = {0xD0, 0xCF, 0x13, 0x2F, 0x64, 0xCC};
// Connect to mac: ac:27:6e:cc:25:b0
//Tornado MAC
static const uint8_t kSensorSrcMac[ESP_NOW_ETH_ALEN] = {0xAC, 0x27, 0x6E, 0xCC, 0x25, 0xB0};
static TickType_t s_lastTargetRxTick = 0;
static constexpr TickType_t kConnTimeoutTicks = pdMS_TO_TICKS(1500);
// ------------------------------------------------------
// -----Function prototypes------------------------------
static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
static bool send_button_state(uint8_t state);
// ------------------------------------------------------
// -----Data structures----------------------------------
carDta car; // Global variable to hold the latest car data received via ESP-NOW (2 motors + 5 sensors)
// ------------------------------------------------------
// -----Tasks--------------------------------------------
void dspTask(void* param);
// ------------------------------------------------------
extern "C" void app_main(void){
    ESP_LOGI("APP_MAIN", "Starting app controller...");
    bLed.begin();// WS2812 onboard LED on many ESP32-S3 Supermini boards is on GPIO48.
    bLed.color(100, 0, 100);// Set initial color to purple to indicate startup
    //-----Buton start/stop on pin 13-----------
    pin.digitalPin("btnAction", 13, GPIO_MODE_INPUT, GPIO_PULLDOWN_ONLY);
    btnAct.set(pin, "btnAction");
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

    s_displayReady = dsp.begin();
    if (!s_displayReady) {
        ESP_LOGE("OLED", "OLED init failed. Check SDA/SCL pins and I2C address.");
    }

    ESP_LOGI("Receiver", "Receptor listo. Esperando mensajes...");
    // -----Task for displyaying data on OLED and controlling RGB LED based on received data-----
    taskMgr.add("DisplayTask",dspTask, NULL, 1, 0, 4096);
}
static void decode_car_payload(const uint8_t *data, int len, carDta &out) {
    auto read_le16 = [data](int idx) -> int16_t {
        return static_cast<int16_t>(
            static_cast<uint16_t>(data[idx]) |
            (static_cast<uint16_t>(data[idx + 1]) << 8));
    };

    // Padded layout with 5 sensors (18-byte):
    //   [0..1]  motL.speed (int16_t LE)
    //   [2]     motL.dir   (int8_t)
    //   [3]     padding
    //   [4..5]  motR.speed (int16_t LE)
    //   [6]     motR.dir   (int8_t)
    //   [7]     padding
    //   [8..9]  sensor.a   (int16_t LE)
    //   [10..11]sensor.b   (int16_t LE)
    //   [12..13]sensor.c   (int16_t LE)
    //   [14..15]sensor.d   (int16_t LE)
    //   [16..17]sensor.e   (int16_t LE)
    if (len == 18) {
        out.motL.speed = read_le16(0);
        out.motL.dir   = static_cast<int8_t>(data[2]);
        // data[3] = padding
        out.motR.speed = read_le16(4);
        out.motR.dir   = static_cast<int8_t>(data[6]);
        // data[7] = padding
        out.sensor.a = (read_le16(8)  != 0);
        out.sensor.b = (read_le16(10) != 0);
        out.sensor.c = (read_le16(12) != 0);
        out.sensor.d = (read_le16(14) != 0);
        out.sensor.e = (read_le16(16) != 0);
        return;
    }

    // Legacy padded layout (14-byte, 3 sensors):
    //   [0..1]  motL.speed (int16_t LE)
    //   [2]     motL.dir   (int8_t)
    //   [3]     padding
    //   [4..5]  motR.speed (int16_t LE)
    //   [6]     motR.dir   (int8_t)
    //   [7]     padding
    //   [8..9]  sensor.a   (int16_t LE)
    //   [10..11]sensor.b   (int16_t LE)
    //   [12..13]sensor.c   (int16_t LE)
    if (len == 14) {
        out.motL.speed = read_le16(0);
        out.motL.dir   = static_cast<int8_t>(data[2]);
        // data[3] = padding
        out.motR.speed = read_le16(4);
        out.motR.dir   = static_cast<int8_t>(data[6]);
        // data[7] = padding
        out.sensor.a = (read_le16(8)  != 0);
        out.sensor.b = (read_le16(10) != 0);
        out.sensor.c = (read_le16(12) != 0);
        out.sensor.d = false;
        out.sensor.e = false;
        return;
    }

    // Fallback packed layout (11-byte for 5 sensors, 9-byte legacy for 3 sensors):
    //   [0..1] motL.speed, [2] motL.dir, [3..4] motR.speed, [5] motR.dir
    //   [6] sensor.a, [7] sensor.b, [8] sensor.c, [9] sensor.d, [10] sensor.e
    out.motL.speed = read_le16(0);
    out.motL.dir   = static_cast<int8_t>(data[2]);
    out.motR.speed = read_le16(3);
    out.motR.dir   = static_cast<int8_t>(data[5]);
    out.sensor.a   = (len > 6 && data[6] != 0);
    out.sensor.b   = (len > 7 && data[7] != 0);
    out.sensor.c   = (len > 8 && data[8] != 0);
    out.sensor.d   = (len > 9 && data[9] != 0);
    out.sensor.e   = (len > 10 && data[10] != 0);
}
static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (memcmp(recv_info->src_addr, kSensorSrcMac, ESP_NOW_ETH_ALEN) != 0) {
        return;
    }
    if (len < 9) {
        ESP_LOGW("REC", "Ignoring short packet from target MAC: " MACSTR ", len=%d", MAC2STR(recv_info->src_addr), len);
        return;
    }

    decode_car_payload(data, len, car);
    memcpy(s_peerMac, recv_info->src_addr, ESP_NOW_ETH_ALEN);
    s_peerKnown = true;
    s_buttonStateSynced = false;
    s_lastTargetRxTick = xTaskGetTickCount();

    ESP_LOGI("Car data", "MAC: " MACSTR " -> Sensor A: %d, Sensor B: %d, Sensor C: %d, Sensor D: %d, Sensor E: %d, speedL: %d, dirL: %d, speedR: %d, dirR: %d",
             MAC2STR(recv_info->src_addr),
             car.sensor.a, car.sensor.b, car.sensor.c, car.sensor.d, car.sensor.e,
             car.motL.speed, car.motL.dir, car.motR.speed, car.motR.dir);
}
static bool send_button_state(uint8_t state) {
    if (!s_peerKnown) {
        return false;
    }

    btnDta payload;
    payload.state = state ? 1 : 0;

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, s_peerMac, ESP_NOW_ETH_ALEN);
    peer_info.channel = 0;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;

    if (!esp_now_is_peer_exist(s_peerMac)) {
        esp_err_t add_ret = esp_now_add_peer(&peer_info);
        if (add_ret != ESP_OK && add_ret != ESP_ERR_ESPNOW_EXIST) {
            ESP_LOGE("ESPNOW", "Failed to add peer: %s", esp_err_to_name(add_ret));
            return false;
        }
    }

    esp_err_t ret = esp_now_send(s_peerMac, reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
    if (ret != ESP_OK) {
        ESP_LOGE("ESPNOW", "Failed to send button state: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}
int8_t cposX=62;
int8_t csepX=15;
int8_t faceOffsetX=4;
int8_t faceOffsetY=0;
struct senSt{
    bool a=false;
    bool b=false;
    bool c=false;
    bool d=false;
    bool e=false;
} stdSens;
void dspTask(void* param){
    bool currentBtnState = btnAct.toggledState();
    bool holdResetLock = false;
    while(true){
        btnAct.update();
        if (btnAct.toggledChanged()) {
            bool nextBtnState = btnAct.toggledState();
            // If reset happened during OFF hold, ignore the next release toggle back to ON.
            if (holdResetLock && nextBtnState) {
                btnAct.setToggledState(false);
                currentBtnState = false;
                holdResetLock = false;
            } else {
                currentBtnState = nextBtnState;
            }
        }
        // Update OLED display with current car data
        if (s_displayReady) {
            bool isConnected = s_peerKnown && ((xTaskGetTickCount() - s_lastTargetRxTick) <= kConnTimeoutTicks);
            dsp.clear();
            // Sensor indicators on first OLED line: empty when false, filled when true.
            stdSens.a = car.sensor.a;
            stdSens.b = car.sensor.b;
            stdSens.c = car.sensor.c;
            stdSens.d = car.sensor.d;
            stdSens.e = car.sensor.e;
            int16_t speedL = car.motL.speed;
            int16_t speedR = car.motR.speed;
            std::string move = "N/A";
            if(speedL==speedR && speedL>0){
                move="FORWARD";
            }else if(speedL<speedR){
                move="LEFT";
            }else if(speedL>speedR){
                move="RIGHT";
            }else if(speedL==0&&speedR==0){
                move="STOPPED";
            }
            // Status face position offset (configurable).
            auto fx = [](int16_t x) { return x + faceOffsetX; };
            auto fy = [](int16_t y) { return y + faceOffsetY; };

            // Status icon frame (slightly larger).
            dsp.drawSensorCircle(fx(10), fy(10), 10, false);
            if (isConnected) {
                // Happy face: eyes and smile.
                dsp.drawPixel(fx(6), fy(8), true);
                dsp.drawPixel(fx(7), fy(8), true);
                dsp.drawPixel(fx(12), fy(8), true);
                dsp.drawPixel(fx(13), fy(8), true);
                dsp.drawLine(fx(8), fy(15), fx(12), fy(15), true);
                dsp.drawPixel(fx(7), fy(14), true);
                dsp.drawPixel(fx(13), fy(14), true);
            } else {
                // Sad face: X eyes and frown.
                dsp.drawLine(fx(5), fy(8), fx(8), fy(11), true);
                dsp.drawLine(fx(8), fy(8), fx(5), fy(11), true);
                dsp.drawLine(fx(11), fy(8), fx(14), fy(11), true);
                dsp.drawLine(fx(14), fy(8), fx(11), fy(11), true);
                dsp.drawLine(fx(8), fy(16), fx(12), fy(16), true);
                dsp.drawPixel(fx(7), fy(17), true);
                dsp.drawPixel(fx(13), fy(17), true);
                /*stdSens.a = false;
                stdSens.b = false;
                stdSens.c = false;
                stdSens.d = false;
                stdSens.e = false;*/
            }
            dsp.drawString(108, 2, std::string(currentBtnState ? "ON" : "OFF"));
            dsp.drawSensorCircle(6+cposX-csepX*2, 5, 5, stdSens.a);
            dsp.drawSensorCircle(6+cposX-csepX, 5, 5, stdSens.b);
            dsp.drawSensorCircle(6+cposX, 5, 5, stdSens.c);
            dsp.drawSensorCircle(6+cposX+csepX, 5, 5, stdSens.d);
            dsp.drawSensorCircle(6+cposX+csepX*2, 5, 5, stdSens.e);
            int yPos=14;
            dsp.drawLine(34,yPos,102,yPos,true);// Separator line below sensor indicators
            dsp.drawLine(34,yPos,34,64,true);// Separator vertical middle line below sensor indicators (1)
            dsp.drawLine(0,yPos+10,102,yPos+10,true);// Separator line below sensor indicators
            dsp.drawLine(68,yPos,68,38,true);// Separator vertical middle line below sensor indicators (2)
            dsp.drawLine(102,yPos,102,64,true);// Separator vertical middle line below sensor indicators (3)
            dsp.drawLine(0,38,102,38,true);// Separator line below speed indicators
            dsp.drawLine(0,52,102,52,true);// Separator line below state indicators
            // dsp.drawLine(0,70,102,70,true);// Separator line below sensor indicators
            dsp.drawString(38, 16, "LEFT");
            dsp.drawString(72, 16, "RIGHT");
            dsp.drawString(0, 28, "Speed");
            dsp.drawString(0, 42,"Dir");
            dsp.drawString(0, 56,"Time");
            dsp.drawString(38, 42,move);
            dsp.drawString(38, 28, std::to_string(speedL));
            dsp.drawString(72, 28, std::to_string(speedR));
            //dsp.drawString(38, 42, std::to_string(car.motL.dir));
            //dsp.drawString(72, 42, std::to_string(car.motR.dir));
            // Stopwatch: runs while button is ON, pauses while OFF.
            // If OFF is held for >3s, reset elapsed time to 0 once per hold.
            static TickType_t startTick = 0;
            static TickType_t pausedTicks = 0;
            static TickType_t btnOffTick = 0;
            static bool longPressResetDone = false;
            const TickType_t nowTick = xTaskGetTickCount();

            if(currentBtnState && startTick == 0){
                startTick = nowTick - pausedTicks;
                pausedTicks = 0;
            }else if(!currentBtnState && startTick != 0){
                pausedTicks = nowTick - startTick;
                startTick = 0;
            }

            if (!currentBtnState) {
                if (btnOffTick == 0) {
                    btnOffTick = nowTick;
                    longPressResetDone = false;
                } else if (!longPressResetDone && (nowTick - btnOffTick) >= pdMS_TO_TICKS(3000)) {
                    startTick = 0;
                    pausedTicks = 0;
                    longPressResetDone = true;
                    holdResetLock = true;
                    btnAct.setToggledState(false);
                    currentBtnState = false;
                }
            } else {
                btnOffTick = 0;
                longPressResetDone = false;
            }

            TickType_t elapsedTicks = currentBtnState ? (nowTick - startTick) : pausedTicks;
            uint32_t elapsedSeconds = pdTICKS_TO_MS(elapsedTicks) / 1000;
            // Show elapsed time in format 00:00:000, minutes:seconds:centiseconds
            int min=elapsedSeconds / 60;
            int sec=elapsedSeconds % 60;
            int centis=(pdTICKS_TO_MS(elapsedTicks) % 1000) / 10;
            
            dsp.drawString(38, 56, std::to_string(min));
            dsp.drawString(52, 56, ":" + std::to_string(sec));
            dsp.drawString(70, 56, ":" + std::to_string(centis));
            
            // Finally, refresh the display to show the new data
            dsp.update();
        }

        // Set RGB LED color based on motor speeds

        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        if(stdSens.a&&!stdSens.b&&!stdSens.c){// Left sensor active only
            red = 255;
            green = 0;
            blue = 0;
        }else if(!stdSens.a&&stdSens.b&&!stdSens.c){// Middle sensor active only
            red = 0;
            green = 255;
            blue = 0;
        }else if(!stdSens.a&&!stdSens.b&&stdSens.c){// Right sensor active only
            red = 0;
            green = 0;
            blue = 255;
        }else if(stdSens.a&&stdSens.b&&!stdSens.c){// Left + middle sensors active
            red = 255;
            green = 255;
            blue = 0;
        }else if(stdSens.a&&!stdSens.b&&stdSens.c){// Left + right sensors active
            red = 255;
            green = 0;
            blue = 255;
        }else if(!stdSens.a&&stdSens.b&&stdSens.c){// Middle + right sensors active
            red = 0;
            green = 255;
            blue = 255;
        }else if(stdSens.a&&stdSens.b&&stdSens.c){// All sensors active
            red = 255;
            green = 255;
            blue = 255;
        }
        bLed.color(red, green, blue);

        vTaskDelay(pdMS_TO_TICKS(10)); // Update every 10ms
    }
}



