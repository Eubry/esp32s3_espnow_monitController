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
// static const uint8_t kSensorSrcMac[ESP_NOW_ETH_ALEN] = {0xD0, 0xCF, 0x13, 0x2F, 0x64, 0xCC};
// Connect to mac: ac:27:6e:cc:25:b0
static const uint8_t kSensorSrcMac[ESP_NOW_ETH_ALEN] = {0xAC, 0x27, 0x6E, 0xCC, 0x25, 0xB0};
static TickType_t s_lastTargetRxTick = 0;
static constexpr TickType_t kConnTimeoutTicks = pdMS_TO_TICKS(1500);
// ------------------------------------------------------
// -----Function prototypes------------------------------
static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
static bool send_button_state(uint8_t state);
// ------------------------------------------------------
// -----Data structures----------------------------------
carDta car; // Global variable to hold the latest car data received via ESP-NOW (2 motors + 3 sensors)
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
    // 14-byte padded layout (sender struct uses compiler-aligned motDta + int16_t sensors):
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
        out.motL.speed = static_cast<int16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
        out.motL.dir   = static_cast<int8_t>(data[2]);
        // data[3] = padding
        out.motR.speed = static_cast<int16_t>(static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8));
        out.motR.dir   = static_cast<int8_t>(data[6]);
        // data[7] = padding
        out.sensor.a = (static_cast<int16_t>(static_cast<uint16_t>(data[8])  | (static_cast<uint16_t>(data[9])  << 8)) != 0);
        out.sensor.b = (static_cast<int16_t>(static_cast<uint16_t>(data[10]) | (static_cast<uint16_t>(data[11]) << 8)) != 0);
        out.sensor.c = (static_cast<int16_t>(static_cast<uint16_t>(data[12]) | (static_cast<uint16_t>(data[13]) << 8)) != 0);
        return;
    }

    // Fallback packed layout (9-byte, no padding, bool sensors):
    //   [0..1] motL.speed, [2] motL.dir, [3..4] motR.speed, [5] motR.dir
    //   [6] sensor.a, [7] sensor.b, [8] sensor.c
    out.motL.speed = static_cast<int16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
    out.motL.dir   = static_cast<int8_t>(data[2]);
    out.motR.speed = static_cast<int16_t>(static_cast<uint16_t>(data[3]) | (static_cast<uint16_t>(data[4]) << 8));
    out.motR.dir   = static_cast<int8_t>(data[5]);
    out.sensor.a   = (len > 6 && data[6] != 0);
    out.sensor.b   = (len > 7 && data[7] != 0);
    out.sensor.c   = (len > 8 && data[8] != 0);
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

    ESP_LOGI("Car data", "MAC: " MACSTR " -> Sensor A: %d, Sensor B: %d, Sensor C: %d, speedL: %d, dirL: %d, speedR: %d, dirR: %d",
             MAC2STR(recv_info->src_addr),
             car.sensor.a, car.sensor.b, car.sensor.c,
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
int8_t cposX=64;
int8_t csepX=20;
struct senSt{
    bool a=false;
    bool b=false;
    bool c=false;
} stdSens;
void dspTask(void* param){
    while(true){
        btnAct.update();
        bool currentBtnState = btnAct.state();
        if (s_peerKnown && (!s_buttonStateSynced || currentBtnState != s_lastBtnState)) {
            send_button_state(currentBtnState);
            s_buttonStateSynced = true;
            s_lastBtnState = currentBtnState;
        } else if (!s_peerKnown) {
            s_lastBtnState = currentBtnState;
        }
        // Update OLED display with current car data
        if (s_displayReady) {
            bool isConnected = s_peerKnown && ((xTaskGetTickCount() - s_lastTargetRxTick) <= kConnTimeoutTicks);
            dsp.clear();
            // Sensor indicators on first OLED line: empty when false, filled when true.
            stdSens.a = !car.sensor.a;
            stdSens.b = !car.sensor.b;
            stdSens.c = !car.sensor.c;
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
            dsp.drawString(0, 2, isConnected ? "ONLINE" : "OFFLINE");
            dsp.drawString(20+cposX+csepX, 2, std::string(btnAct.state()? "ON" : "OFF"));
            dsp.drawSensorCircle(6+cposX-csepX, 5, 5, stdSens.a);
            dsp.drawSensorCircle(6+cposX, 5, 5, stdSens.b);
            dsp.drawSensorCircle(6+cposX+csepX, 5, 5, stdSens.c);
            int yPos=14;
            dsp.drawLine(34,yPos,102,yPos,true);// Separator line below sensor indicators
            dsp.drawLine(34,yPos,34,49,true);// Separator vertical middle line below sensor indicators
            dsp.drawLine(0,yPos+10,102,yPos+10,true);// Separator line below sensor indicators
            dsp.drawLine(68,yPos,68,50,true);// Separator vertical middle line below sensor indicators
            dsp.drawLine(102,yPos,102,50,true);// Separator vertical middle line below sensor indicators
            dsp.drawLine(0,50,102,50,true);// Separator line below sensor indicators
            // dsp.drawLine(0,70,102,70,true);// Separator line below sensor indicators
            dsp.drawString(38, 16, "LEFT");
            dsp.drawString(72, 16, "RIGHT");
            dsp.drawString(0, 28, "Speed");
            dsp.drawString(0, 40,"Dir");
            dsp.drawString(2, 56,move);
            dsp.drawString(38, 28, std::to_string(speedL));
            dsp.drawString(72, 28, std::to_string(speedR));
            dsp.drawString(38, 40, std::to_string(car.motL.dir));
            dsp.drawString(72, 40, std::to_string(car.motR.dir));
            // Cronometer that starts when the button is ON and pauses when the button is OFF. Displaying elapsed time in seconds on the bottom right of the OLED.
            // When the button is pressed for more than 3 seconds the time resets to 0. (This can be used to measure lap times or time spent in a certain state)
            static TickType_t startTick = 0;
            static TickType_t pausedTicks = 0;
            if(btnAct.state() && startTick == 0){
                startTick = xTaskGetTickCount() - pausedTicks;
                pausedTicks = 0;
            }else if(!btnAct.state() && startTick != 0){
                pausedTicks = xTaskGetTickCount() - startTick;
                startTick = 0;
            }
            TickType_t elapsedTicks = btnAct.state() ? (xTaskGetTickCount() - startTick) : pausedTicks;
            uint32_t elapsedSeconds = pdTICKS_TO_MS(elapsedTicks) / 1000;
            
            dsp.drawString(50, 56, "Time: " + std::to_string(elapsedSeconds) + "s");
            
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



