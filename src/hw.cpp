#include "hw.h"

const byte DNS_PORT = 53;

// ── WiFi persistence ──────────────────────────────────────────────────────────
void saveWifiNets() {
    Preferences p; p.begin("furby", false);
    p.putInt("wifi_count", wifiNetCount);
    for (int i = 0; i < wifiNetCount; i++) {
        p.putString(("ws" + String(i)).c_str(), wifiNets[i].ssid);
        p.putString(("wp" + String(i)).c_str(), wifiNets[i].pass);
    }
    p.end();
}

void loadWifiNets() {
    // chiamata durante boot con preferences globale già aperto
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
static void i2cBusRecover() {
    // SCL clock stretch recovery: 9 impulsi manuali per sbloccare slave bloccati
    Wire.end();
    pinMode(I2C_SCL_PIN, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(10);
        digitalWrite(I2C_SCL_PIN, LOW);  delayMicroseconds(10);
    }
    digitalWrite(I2C_SCL_PIN, HIGH);
    pinMode(I2C_SCL_PIN, INPUT);
    delayMicroseconds(10);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
    delay(10);
}

void ch32WritePort(uint8_t value) {
    uint8_t data[2] = {0x03, value};
    Wire.beginTransmission(CH32_ADDR);
    Wire.write(data, 2);
    uint8_t err = Wire.endTransmission();
    if (err) {
        Serial.printf("IO exp err=%d port=0x%02X - tentativo recovery I2C\n", err, value);
        i2cBusRecover();
        Wire.beginTransmission(CH32_ADDR);
        Wire.write(data, 2);
        err = Wire.endTransmission();
        if (err) Serial.printf("IO exp: recovery fallito err=%d\n", err);
    }
}

void ch32SetBit(uint8_t bit, bool val) {
    if (val) ch32PortState |=  (1 << bit);
    else     ch32PortState &= ~(1 << bit);
    ch32WritePort(ch32PortState);
}

void ch32Init() {
    uint8_t modeData[2] = {0x02, 0xFF};
    Wire.beginTransmission(CH32_ADDR);
    Wire.write(modeData, 2);
    uint8_t err = Wire.endTransmission();
    if (err) Serial.printf("IO exp init mode err=%d\n", err);
    ch32PortState = 0x00;
    ch32WritePort(ch32PortState);
    ch32SetBit(4, true); // IO4 HIGH - SD card CS
    ch32SetBit(6, true); // IO6 HIGH - amplificatore sempre ON
    Serial.println("IO expander: init OK, amp ON");
}


int readBatteryMv() {
    static bool batFailed = false;
    if (batFailed) return -1;
    Wire.beginTransmission(CH32_ADDR);
    Wire.write(0x06);
    if (Wire.endTransmission(false) != 0) { batFailed = true; return -1; }
    if (Wire.requestFrom((uint8_t)CH32_ADDR, (uint8_t)2) != 2) { batFailed = true; return -1; }
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

// ── SD card mount/unmount/hotplug ─────────────────────────────────────────────

bool sdMount() {
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    bool ok = SD_MMC.begin("/sdcard", true, false, 20000);
    if (!ok) {
        SD_MMC.end();
        vTaskDelay(50 / portTICK_PERIOD_MS);
        ok = SD_MMC.begin("/sdcard", true, false, 4000);
    }
    sdAvailable = ok;
    if (ok) {
        // cardSize() può essere instabile subito dopo begin() - aspetta che si stabilizzi
        uint64_t sz = 0;
        for (int i = 0; i < 5 && sz == 0; i++) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            sz = SD_MMC.cardSize();
        }
        uint8_t t = SD_MMC.cardType();
        const char* ts = (t==CARD_MMC)?"MMC":(t==CARD_SD)?"SDSC":(t==CARD_SDHC)?"SDHC":"UNK";
        Serial.printf("SD: montata  tipo=%s  %lluMB card  %lluMB usati\n",
            ts, sz/(1024*1024), SD_MMC.usedBytes()/(1024*1024));
    } else {
        Serial.println("SD: mount fallito");
    }
    return ok;
}

void sdUnmount() {
    if (!sdAvailable) return;
    SD_MMC.end();
    sdAvailable = false;
    Serial.println("SD: smontata");
}

// Chiamata prima di ogni accesso SD e al caricamento di /sys/info.
// Se montata e card non risponde → unmount. Se non montata → prova mount.
bool sdCheck() {
    if (sdAvailable) {
        if (SD_MMC.cardType() == CARD_NONE) {
            Serial.println("SD: rimossa - modalita RAM streaming");
            sdUnmount();
        }
    } else {
        sdMount();
    }
    return sdAvailable;
}

// ── Camera ────────────────────────────────────────────────────────────────────
void camApplyExposure(int gainCeiling, int brightness, int agc) {
    if (!camActive) return;
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    s->set_brightness(s, constrain(brightness, -2, 2));
    if (agc) {
        // auto: limita il ceiling per evitare sovraesposizione
        s->set_gain_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_gainceiling(s, (gainceiling_t)constrain(gainCeiling, 0, 6));
    } else {
        // manuale: disabilita AGC/AEC e imposta gain fisso proporzionale al ceiling scelto
        s->set_gain_ctrl(s, 0);
        s->set_aec2(s, 0);
        s->set_agc_gain(s, gainCeiling * 5); // 0-30, scala su 0-6
        s->set_aec_value(s, 300);            // esposizione manuale moderata
    }
}

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
    if (psramFound()) { cam.frame_size = camStreamSize;   cam.jpeg_quality = camStreamQuality; cam.fb_count = 2; cam.fb_location = CAMERA_FB_IN_PSRAM; }
    else              { cam.frame_size = FRAMESIZE_QQVGA; cam.jpeg_quality = 12;                cam.fb_count = 1; cam.fb_location = CAMERA_FB_IN_DRAM; }
    camActive = (esp_camera_init(&cam) == ESP_OK);
    Serial.println(camActive ? "CAM: init OK" : "CAM: init FALLITA");
    if (camActive) {
        camApplySettings(camStreamSize, camStreamQuality);
        camApplyFlicker(camFlicker);
        camApplyExposure(camGainCeiling, camBrightness, camAgc);
    }
    return camActive;
}

void camDeinit() {
    if (!camActive) return;
    camActive = false;
    esp_camera_deinit();
    Serial.println("CAM: deinit");
}

void camApplySettings(framesize_t size, int quality) {
    if (!camActive) return;
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    s->set_framesize(s, size);
    s->set_quality(s, quality);
}

void camApplyFlicker(int hz) {
    if (!camActive) return;
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    // OV2640 anti-banding: 0=disable, 1=50Hz, 2=60Hz
    int mode = (hz == 50) ? 1 : (hz == 60) ? 2 : 0;
    s->set_bpc(s, mode == 0 ? 0 : 1);
    s->set_wpc(s, mode == 0 ? 0 : 1);
    s->set_lenc(s, mode == 0 ? 0 : 1);
    if (s->set_gain_ctrl) s->set_gain_ctrl(s, mode == 0 ? 1 : 0);
    if (s->set_aec2)      s->set_aec2(s, mode != 0 ? 1 : 0);
    // light frequency hint via raw register (0xA8): 0=50Hz, 1=60Hz
    if (mode != 0) {
        s->set_reg(s, 0xA8, 0xFF, (mode == 2) ? 1 : 0);
    }
}
