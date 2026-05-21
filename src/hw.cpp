#include "hw.h"

const byte DNS_PORT = 53;

// ── WiFi persistence ──────────────────────────────────────────────────────────
void saveWifiNets() {
    preferences.putInt("wifi_count", wifiNetCount);
    for (int i = 0; i < wifiNetCount; i++) {
        preferences.putString(("ws" + String(i)).c_str(), wifiNets[i].ssid);
        preferences.putString(("wp" + String(i)).c_str(), wifiNets[i].pass);
    }
}

void loadWifiNets() {
    wifiNetCount = preferences.getInt("wifi_count", 0);
    if (wifiNetCount > MAX_WIFI_NETS) wifiNetCount = MAX_WIFI_NETS;
    for (int i = 0; i < wifiNetCount; i++) {
        wifiNets[i].ssid = preferences.getString(("ws" + String(i)).c_str(), "");
        wifiNets[i].pass = preferences.getString(("wp" + String(i)).c_str(), "");
    }
}

void upsertWifiNet(const String& ssid, const String& pass, int priority) {
    for (int i = 0; i < wifiNetCount; i++) {
        if (wifiNets[i].ssid == ssid) {
            wifiNets[i].pass = pass;
            WifiNet tmp = wifiNets[i];
            for (int j = i; j > priority; j--) wifiNets[j] = wifiNets[j-1];
            wifiNets[priority] = tmp;
            saveWifiNets();
            return;
        }
    }
    if (wifiNetCount < MAX_WIFI_NETS) {
        int pos = min(priority, wifiNetCount);
        for (int j = wifiNetCount; j > pos; j--) wifiNets[j] = wifiNets[j-1];
        wifiNets[pos] = {ssid, pass};
        wifiNetCount++;
        saveWifiNets();
    }
}

void removeWifiNet(int idx) {
    if (idx < 0 || idx >= wifiNetCount) return;
    for (int i = idx; i < wifiNetCount - 1; i++) wifiNets[i] = wifiNets[i+1];
    wifiNetCount--;
    saveWifiNets();
}

bool tryConnectWifi(bool useDelay) {
    WiFi.disconnect(true);
    if (useDelay) vTaskDelay(300/portTICK_PERIOD_MS); else delay(300);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    for (int i = 0; i < wifiNetCount; i++) {
        Serial.println("WiFi: provo " + wifiNets[i].ssid);
        WiFi.begin(wifiNets[i].ssid.c_str(), wifiNets[i].pass.c_str());
        for (int t = 0; t < 20 && WiFi.status() != WL_CONNECTED; t++)
            if (useDelay) vTaskDelay(500/portTICK_PERIOD_MS); else delay(500);
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi connesso: " + WiFi.localIP().toString());
            return true;
        }
        WiFi.disconnect(true);
        if (useDelay) vTaskDelay(200/portTICK_PERIOD_MS); else delay(200);
    }
    return false;
}

void startCaptivePortal() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Furby_Config");
    delay(500);
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    isConfigMode = true;
    Serial.println("Captive portal attivo: " + WiFi.softAPIP().toString());
}

// ── IO expander CH32 ─────────────────────────────────────────────────────────
void ch32WritePort(uint8_t value) {
    uint8_t data[2] = {0x03, value};
    Wire.beginTransmission(CH32_ADDR);
    uint8_t written = Wire.write(data, 2);
    uint8_t err = Wire.endTransmission();
    Serial.printf("IO exp write: port=0x%02X written=%d err=%d\n", value, written, err);
}

void ch32SetBit(uint8_t bit, bool val) {
    if (val) ch32PortState |=  (1 << bit);
    else     ch32PortState &= ~(1 << bit);
    ch32WritePort(ch32PortState);
}

void ch32Init() {
    uint8_t modeData[2] = {0x02, 0xFF};
    Wire.beginTransmission(CH32_ADDR);
    uint8_t written = Wire.write(modeData, 2);
    uint8_t err = Wire.endTransmission();
    Serial.printf("IO exp init mode: written=%d err=%d\n", written, err);
    ch32PortState = 0x00;
    ch32WritePort(ch32PortState);
    ch32SetBit(6, true);  // PA enable on
    Serial.println("IO expander: init OK, IO6=1");
}

void setAmplifier(bool enable) {
    Serial.printf("setAmplifier(%s) port sarà 0x%02X\n", enable?"ON":"OFF",
        enable ? (ch32PortState | (1<<6)) : (ch32PortState & ~(1<<6)));
    ch32SetBit(6, enable);
}

int readBatteryMv() {
    Wire.beginTransmission(CH32_ADDR);
    Wire.write(0x06);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((uint8_t)CH32_ADDR, (uint8_t)2) != 2) return -1;
    uint8_t lo = Wire.read(), hi = Wire.read();
    uint16_t raw = (uint16_t)(hi << 8 | lo);
    return (int)((uint32_t)raw * 3300 * 2 / 4095);
}

// ── ES8311 DAC ────────────────────────────────────────────────────────────────
void es8311WriteReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg); Wire.write(val);
    uint8_t err = Wire.endTransmission();
    if (err) Serial.printf("ES8311 I2C err reg=0x%02X val=0x%02X err=%d\n", reg, val, err);
}

void initES8311() {
    Serial.println("ES8311: init...");
    es8311WriteReg(ES8311_RESET_REG00, 0x1F); delay(20);
    es8311WriteReg(ES8311_RESET_REG00, 0x00);
    es8311WriteReg(ES8311_RESET_REG00, 0x80);
    es8311WriteReg(ES8311_CLK_MANAGER_REG01, 0x3F);
    es8311WriteReg(ES8311_CLK_MANAGER_REG02, 0x00);
    es8311WriteReg(ES8311_CLK_MANAGER_REG03, 0x10);
    es8311WriteReg(ES8311_CLK_MANAGER_REG04, 0x10);
    es8311WriteReg(ES8311_CLK_MANAGER_REG05, 0x00);
    es8311WriteReg(ES8311_CLK_MANAGER_REG06, 0x03);
    es8311WriteReg(ES8311_CLK_MANAGER_REG07, 0x00);
    es8311WriteReg(ES8311_CLK_MANAGER_REG08, 0xFF);
    es8311WriteReg(ES8311_SDPIN_REG09,  0x0C);
    es8311WriteReg(ES8311_SDPOUT_REG0A, 0x0C);
    es8311WriteReg(ES8311_SYSTEM_REG0D, 0x01);
    es8311WriteReg(ES8311_SYSTEM_REG0E, 0x02);
    es8311WriteReg(ES8311_SYSTEM_REG12, 0x00);
    es8311WriteReg(ES8311_SYSTEM_REG13, 0x10);
    es8311WriteReg(ES8311_ADC_REG1C,    0x6A);
    es8311WriteReg(ES8311_DAC_REG37,    0x08);
    es8311WriteReg(ES8311_DAC_REG31,    0x00);
    es8311WriteReg(ES8311_DAC_REG32,    186);
    es8311WriteReg(ES8311_GP_REG45,     0x00);
    Serial.println("ES8311: init OK, volume=75%");
}

// ── ES7210 ADC mic ────────────────────────────────────────────────────────────
void es7210WriteReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES7210_ADDR);
    Wire.write(reg); Wire.write(val);
    uint8_t err = Wire.endTransmission();
    if (err) Serial.printf("ES7210 I2C err reg=0x%02X val=0x%02X err=%d\n", reg, val, err);
}

void initES7210() {
    Serial.println("ES7210: init...");
    es7210WriteReg(ES7210_RESET_REG00, 0xFF); delay(20);
    es7210WriteReg(ES7210_RESET_REG00, 0x32);
    es7210WriteReg(0x09, 0x30);
    es7210WriteReg(0x0A, 0x30);
    es7210WriteReg(ES7210_ADC12_HPF1_REG23, 0x2A);
    es7210WriteReg(ES7210_ADC12_HPF2_REG22, 0x0A);
    es7210WriteReg(ES7210_ADC34_HPF1_REG21, 0x2A);
    es7210WriteReg(ES7210_ADC34_HPF2_REG20, 0x0A);
    es7210WriteReg(ES7210_SDP_IFACE1_REG11, 0x60);
    es7210WriteReg(ES7210_SDP_IFACE2_REG12, 0x00);
    es7210WriteReg(ES7210_ANALOG_REG40,     0xC3);
    es7210WriteReg(ES7210_MIC12_BIAS_REG41, 0x70);
    es7210WriteReg(ES7210_MIC34_BIAS_REG42, 0x70);
    es7210WriteReg(ES7210_MIC1_GAIN_REG43,  0x1A);
    es7210WriteReg(ES7210_MIC2_GAIN_REG44,  0x1A);
    es7210WriteReg(0x45, 0x1A);
    es7210WriteReg(0x46, 0x1A);
    es7210WriteReg(ES7210_MIC1_POWER_REG47, 0x08);
    es7210WriteReg(ES7210_MIC2_POWER_REG48, 0x08);
    es7210WriteReg(0x49, 0x08);
    es7210WriteReg(0x4A, 0x08);
    es7210WriteReg(ES7210_OSR_REG07,        0x20);
    es7210WriteReg(ES7210_MAINCLK_REG02,    0xC1);
    es7210WriteReg(ES7210_LRCK_DIVH_REG04,  0x01);
    es7210WriteReg(ES7210_LRCK_DIVL_REG05,  0x00);
    es7210WriteReg(ES7210_POWER_DOWN_REG06, 0x04);
    es7210WriteReg(ES7210_MIC12_POWER_REG4B, 0x0F);
    es7210WriteReg(ES7210_MIC34_POWER_REG4C, 0x0F);
    es7210WriteReg(ES7210_RESET_REG00, 0x71);
    es7210WriteReg(ES7210_RESET_REG00, 0x41);
    Serial.println("ES7210: init OK");
}

// ── I2S ──────────────────────────────────────────────────────────────────────
void initI2S() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate          = 16000,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = true,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 4096000
    };
    i2s_pin_config_t pins = {
        .mck_io_num   = I2S_MCLK,
        .bck_io_num   = I2S_BCLK,
        .ws_io_num    = I2S_LRCK,
        .data_out_num = I2S_DOUT,
        .data_in_num  = I2S_DIN
    };
    i2s_driver_install(I2S_NUM, &cfg, 0, NULL);
    i2s_set_pin(I2S_NUM, &pins);
    i2s_set_clk(I2S_NUM, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

// ── Camera ────────────────────────────────────────────────────────────────────
bool camInit() {
    if (camActive) return true;
    camera_config_t cam;
    cam.ledc_channel  = LEDC_CHANNEL_0; cam.ledc_timer = LEDC_TIMER_0;
    cam.pin_d0 = Y2_GPIO_NUM; cam.pin_d1 = Y3_GPIO_NUM;
    cam.pin_d2 = Y4_GPIO_NUM; cam.pin_d3 = Y5_GPIO_NUM;
    cam.pin_d4 = Y6_GPIO_NUM; cam.pin_d5 = Y7_GPIO_NUM;
    cam.pin_d6 = Y8_GPIO_NUM; cam.pin_d7 = Y9_GPIO_NUM;
    cam.pin_xclk      = XCLK_GPIO_NUM;
    cam.pin_pclk      = PCLK_GPIO_NUM;
    cam.pin_vsync     = VSYNC_GPIO_NUM;
    cam.pin_href      = HREF_GPIO_NUM;
    cam.pin_sccb_sda  = SIOD_GPIO_NUM;
    cam.pin_sccb_scl  = SIOC_GPIO_NUM;
    cam.pin_pwdn      = PWDN_GPIO_NUM;
    cam.pin_reset     = RESET_GPIO_NUM;
    cam.xclk_freq_hz  = 20000000;
    cam.pixel_format  = PIXFORMAT_JPEG;
    if (psramFound()) { cam.frame_size = FRAMESIZE_QVGA;  cam.jpeg_quality = 12; cam.fb_count = 2; cam.fb_location = CAMERA_FB_IN_PSRAM; }
    else              { cam.frame_size = FRAMESIZE_QQVGA; cam.jpeg_quality = 12; cam.fb_count = 1; cam.fb_location = CAMERA_FB_IN_DRAM; }
    camActive = (esp_camera_init(&cam) == ESP_OK);
    Serial.println(camActive ? "CAM: init OK" : "CAM: init FALLITA");
    return camActive;
}

void camDeinit() {
    if (!camActive) return;
    esp_camera_deinit();
    camActive = false;
    Serial.println("CAM: deinit");
}
