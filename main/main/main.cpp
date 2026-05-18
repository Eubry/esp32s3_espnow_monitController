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
static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len == sizeof(carDta)) {
        memcpy(&car, data, sizeof(car));
        memcpy(s_peerMac, recv_info->src_addr, ESP_NOW_ETH_ALEN);
        s_peerKnown = true;
        s_buttonStateSynced = false;
        
        ESP_LOGI("Car data", "MAC: " MACSTR " -> Velocidad izquierda: %d, Dirección izquierda: %d, Velocidad derecha: %d, Dirección derecha: %d",
                 MAC2STR(recv_info->src_addr),
                 car.motL.speed, car.motL.dir, car.motR.speed, car.motR.dir);
    } else {
        ESP_LOGW("REC", "Paquete recibido con tamaño inesperado: %d", len);
    }
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
            dsp.clear();
            // Sensor indicators on first OLED line: empty when false, filled when true.
            dsp.drawString(0, 2, "CONN");
            dsp.drawSensorCircle(cposX-csepX, 5, 5, btnAct.state());// Sensor A is always active (true) since we don't receive its state, so we show it as a filled circle.
            dsp.drawSensorCircle(cposX, 5, 5, false);// Sensor B state is unknown (not sent by car), so we show it as an empty circle.
            dsp.drawSensorCircle(cposX+csepX, 5, 5, car.sensor.c);
            dsp.drawLine(0,13,128,13,true);// Separator line below sensor indicators
            dsp.drawString(0, 15, "L Speed: " + std::to_string(car.motL.speed));
            dsp.drawString(0, 25, "L Dir: " + std::to_string(car.motL.dir));
            dsp.drawString(0, 35, "R Speed: " + std::to_string(car.motR.speed));
            dsp.drawString(0, 45, "R Dir: " + std::to_string(car.motR.dir));
            dsp.drawString(0, 55, "Button: " + std::string(btnAct.state()? "ON" : "OFF"));
            dsp.update();
        }

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