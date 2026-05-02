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
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <ArduinoJson.h>

// --- Configuration (edit before flashing) ----------------
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* SERVER_IP = "192.168.1.100"; // PC running server.py
const uint16_t SERVER_PORT = 50123;
const int LV_TICK_MS = 5;

// --- Globals ------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[480 * 20]; // small buffer strip

WiFiClient client;

// LVGL objects
lv_obj_t* label_cpu_temp;
lv_obj_t* label_cpu_usage;
lv_obj_t* label_gpu_usage;

// --- LVGL flush callback using TFT_eSPI --------------------
void my_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  int32_t w = (area->x2 - area->x1 + 1);
  int32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // TFT_eSPI expects 16-bit color; LVGL default lv_color_t is 16-bit on many builds
  tft.pushColors((uint16_t*)color_p, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// LVGL tick
void lv_tick_task(void* arg) {
  (void)arg;
  lv_tick_inc(LV_TICK_MS);
}

// Connect to PC server and keep client open
void ensure_client_connected() {
  if (client.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  client.stop();
  if (!client.connect(SERVER_IP, SERVER_PORT)) {
    // failed — will retry later
    return;
  }
}

// Parse a newline-delimited JSON line from server
void handle_server_line(const String& line) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) return;
  // Extract values (may be null)
  const char* ts = doc["timestamp"] | "";
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
      if (line.length() > 1024) line = ""; // safety
    }
  }
}

// Create a simple UI
void ui_create() {
  lv_obj_t* scr = lv_disp_get_scr_act(NULL);
  label_cpu_temp = lv_label_create(scr);
  lv_obj_align(label_cpu_temp, LV_ALIGN_TOP_MID, 0, 20);
  lv_label_set_text(label_cpu_temp, "CPU Temp: --");

  label_cpu_usage = lv_label_create(scr);
  lv_obj_align(label_cpu_usage, LV_ALIGN_TOP_MID, 0, 60);
  lv_label_set_text(label_cpu_usage, "CPU Usage: --");

  label_gpu_usage = lv_label_create(scr);
  lv_obj_align(label_gpu_usage, LV_ALIGN_TOP_MID, 0, 100);
  lv_label_set_text(label_gpu_usage, "GPU Usage: --");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("StatDisplay starting...");

  // TFT init
  tft.init();
  tft.setRotation(0); // adjust as needed
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
}

void loop() {
  // LVGL handler
  lv_timer_handler(); // requires recent lvgl Arduino binding

  // Ensure TCP client
  ensure_client_connected();
  read_from_server();

  delay(10);
}
