/*
  StatDisplay.ino
  Arduino sketch for ESP32-S3 using LVGL + TFT_eSPI to display PC stats.

  - Hardcoded WiFi SSID/PASSWORD below
  - Hardcoded PC server IP/PORT below
  - Dependencies: LVGL, TFT_eSPI, ArduinoJson

  Adjust TFT_eSPI User_Setup for your 480x480 panel.
*/

#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <ArduinoJson.h>

#if __has_include("DFRobot_HumanPresenceSensor.h")
#include "DFRobot_HumanPresenceSensor.h"
#endif

// --- Configuration (edit before flashing) ----------------
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* SERVER_IP = "192.168.1.100"; // PC running server.py
const uint16_t SERVER_PORT = 50123;
const int LV_TICK_MS = 5;

// mmWave human-presence sensor configuration
// Most DFRobot Gravity mmWave sensors expose a state byte at this address.
// If your specific sensor uses a different I2C address, change this value.
const uint8_t MMWAVE_SENSOR_ADDR = 0x57;
const uint8_t MMWAVE_STATUS_REG = 0x00;
const uint32_t DATA_STALE_MS = 7000;
const uint32_t HUMAN_ABSENT_DELAY_MS = 2000;

// --- Globals ------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[480 * 20]; // small buffer strip

WiFiClient client;

// LVGL objects
lv_obj_t* label_cpu_temp;
lv_obj_t* label_cpu_usage;
lv_obj_t* label_gpu_usage;
lv_obj_t* idle_card;
lv_obj_t* idle_title;
lv_obj_t* idle_message;

bool screen_is_on = true;
bool human_present = false;
uint32_t last_valid_data_ms = 0;
uint32_t last_human_seen_ms = 0;

// --- LVGL flush callback using TFT_eSPI --------------------
void my_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  int32_t w = (area->x2 - area->x1 + 1);
  int32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)color_p, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// LVGL tick
void lv_tick_task(void* arg) {
  (void)arg;
  lv_tick_inc(LV_TICK_MS);
}

void set_display_power(bool enabled) {
  if (screen_is_on == enabled) return;

  screen_is_on = enabled;

#if defined(TFT_BL)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, enabled ? HIGH : LOW);
#endif

  if (enabled) {
#if defined(TFT_SLPOUT)
    tft.writecommand(TFT_SLPOUT);
#endif
#if defined(TFT_DISPON)
    tft.writecommand(TFT_DISPON);
#endif
  } else {
#if defined(TFT_SLPIN)
    tft.writecommand(TFT_SLPIN);
#endif
  }

  tft.fillScreen(TFT_BLACK);
}

bool read_i2c_reg_u8(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(MMWAVE_SENSOR_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MMWAVE_SENSOR_ADDR, 1u) != 1) return false;
  if (!Wire.available()) return false;
  value = Wire.read();
  return true;
}

bool read_presence_sensor() {
  uint8_t v = 0;

  // If the sensor is not present on the I2C bus, keep the display alive instead of turning it black.
  if (!read_i2c_reg_u8(MMWAVE_STATUS_REG, v)) {
    Serial.println("mmWave sensor not detected on I2C bus - leaving display on.");
    return true;
  }

  // Many radar products expose a non-zero status value when a person is present.
  if (v != 0) return true;

  // Some modules expose the actual state in adjacent registers, so do a quick check.
  for (uint8_t reg = 0x01; reg <= 0x03; ++reg) {
    if (read_i2c_reg_u8(reg, v) && v != 0) {
      return true;
    }
  }

  return false;
}

void update_idle_ui() {
  const bool data_fresh = (millis() - last_valid_data_ms) < DATA_STALE_MS;

  if (data_fresh) {
    lv_obj_add_flag(idle_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(label_cpu_temp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(label_cpu_usage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(label_gpu_usage, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(idle_card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(label_cpu_temp, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(label_cpu_usage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(label_gpu_usage, LV_OBJ_FLAG_HIDDEN);

  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(idle_title, "WiFi is quiet");
    lv_label_set_text(idle_message, "The WiFi link is down or reconnecting.");
  } else if (!client.connected()) {
    lv_label_set_text(idle_title, "Waiting for PC");
    lv_label_set_text(idle_message, "The computer is offline or not streaming stats yet.");
  } else {
    lv_label_set_text(idle_title, "No data yet");
    lv_label_set_text(idle_message, "Still waiting for the next stats packet.");
  }
}

// Connect to PC server and keep client open
void ensure_client_connected() {
  if (client.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  client.stop();
  if (!client.connect(SERVER_IP, SERVER_PORT)) {
    return;
  }
}

// Parse a newline-delimited JSON line from server
void handle_server_line(const String& line) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) return;

  last_valid_data_ms = millis();

  if (doc.containsKey("cpu_temp") && !doc["cpu_temp"].isNull()) {
    float t = doc["cpu_temp"].as<float>();
    char buftxt[64];
    snprintf(buftxt, sizeof(buftxt), "CPU Temp: %.1f C", t);
    lv_label_set_text(label_cpu_temp, buftxt);
  } else {
    lv_label_set_text(label_cpu_temp, "CPU Temp: N/A");
  }

  if (doc.containsKey("cpu_usage") && !doc["cpu_usage"].isNull()) {
    float u = doc["cpu_usage"].as<float>();
    char buftxt[64];
    snprintf(buftxt, sizeof(buftxt), "CPU Usage: %.0f%%", u);
    lv_label_set_text(label_cpu_usage, buftxt);
  } else {
    lv_label_set_text(label_cpu_usage, "CPU Usage: N/A");
  }

  if (doc.containsKey("gpu_usage") && !doc["gpu_usage"].isNull()) {
    float g = doc["gpu_usage"].as<float>();
    char buftxt[64];
    snprintf(buftxt, sizeof(buftxt), "GPU Usage: %.0f%%", g);
    lv_label_set_text(label_gpu_usage, buftxt);
  } else {
    lv_label_set_text(label_gpu_usage, "GPU Usage: N/A");
  }

  update_idle_ui();
}

// Read lines from TCP client and parse
void read_from_server() {
  if (!client.connected()) return;
  static String line;
  while (client.available()) {
    char c = client.read();
    if (c == '\n') {
      if (line.length() > 0) {
        handle_server_line(line);
      }
      line = "";
    } else if (c >= 32) {
      line += c;
      if (line.length() > 1024) line = "";
    }
  }
}

void create_idle_screen() {
  lv_obj_t* scr = lv_disp_get_scr_act(NULL);

  idle_card = lv_obj_create(scr);
  lv_obj_set_size(idle_card, 360, 220);
  lv_obj_center(idle_card);
  lv_obj_set_style_bg_color(idle_card, lv_color_hex(0x122736), 0);
  lv_obj_set_style_border_color(idle_card, lv_color_hex(0x3f5b7d), 0);
  lv_obj_set_style_border_width(idle_card, 1, 0);
  lv_obj_set_style_radius(idle_card, 24, 0);
  lv_obj_set_style_pad_all(idle_card, 18, 0);

  idle_title = lv_label_create(idle_card);
  lv_obj_align(idle_title, LV_ALIGN_TOP_MID, 0, 40);
  lv_label_set_text(idle_title, "Waiting for PC");
  lv_obj_set_style_text_font(idle_title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(idle_title, lv_color_hex(0xE7F3FF), 0);

  idle_message = lv_label_create(idle_card);
  lv_obj_align(idle_message, LV_ALIGN_CENTER, 0, 12);
  lv_label_set_text(idle_message, "The computer is offline or not streaming stats yet.");
  lv_obj_set_style_text_align(idle_message, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(idle_message, lv_color_hex(0xBBD1E8), 0);
  lv_obj_set_width(idle_message, 260);
  lv_label_set_long_mode(idle_message, LV_LABEL_LONG_WRAP);
}

// Create a simple UI
void ui_create() {
  lv_obj_t* scr = lv_disp_get_scr_act(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x081821), 0);

  label_cpu_temp = lv_label_create(scr);
  lv_obj_align(label_cpu_temp, LV_ALIGN_TOP_MID, 0, 30);
  lv_label_set_text(label_cpu_temp, "CPU Temp: --");
  lv_obj_set_style_text_color(label_cpu_temp, lv_color_hex(0xEAF4FF), 0);

  label_cpu_usage = lv_label_create(scr);
  lv_obj_align(label_cpu_usage, LV_ALIGN_TOP_MID, 0, 75);
  lv_label_set_text(label_cpu_usage, "CPU Usage: --");
  lv_obj_set_style_text_color(label_cpu_usage, lv_color_hex(0xEAF4FF), 0);

  label_gpu_usage = lv_label_create(scr);
  lv_obj_align(label_gpu_usage, LV_ALIGN_TOP_MID, 0, 120);
  lv_label_set_text(label_gpu_usage, "GPU Usage: --");
  lv_obj_set_style_text_color(label_gpu_usage, lv_color_hex(0xEAF4FF), 0);

  create_idle_screen();
  update_idle_ui();
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("StatDisplay starting...");

  // I2C for mmWave sensor
  Wire.begin();
  Wire.setClock(400000L);

  // TFT init
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // LVGL init
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, 480 * 20);
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 480;
  disp_drv.ver_res = 480;
  disp_drv.flush_cb = my_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Create UI
  ui_create();

  // LVGL periodic tick
  const esp_timer_create_args_t tick_timer_args = {
    .callback = [](void*) { lv_tick_inc(LV_TICK_MS); },
    .name = "lv_tick"
  };
  esp_timer_handle_t tick_timer;
  esp_timer_create(&tick_timer_args, &tick_timer);
  esp_timer_start_periodic(tick_timer, LV_TICK_MS * 1000);

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries++ < 40) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed to connect");
  }

  set_display_power(true);
}

void loop() {
  static uint32_t last_sensor_poll_ms = 0;

  lv_timer_handler();

  if (millis() - last_sensor_poll_ms >= 250) {
    last_sensor_poll_ms = millis();

    bool sensor_detected_human = read_presence_sensor();
    if (sensor_detected_human) {
      last_human_seen_ms = millis();
      human_present = true;
      set_display_power(true);
    } else if (millis() - last_human_seen_ms > HUMAN_ABSENT_DELAY_MS) {
      human_present = false;
      set_display_power(false);
    }
  }

  if (screen_is_on) {
    ensure_client_connected();
    read_from_server();
    update_idle_ui();
  }

  delay(10);
}
