#include <TFT_eSPI.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include "esp_camera.h"

// ================ CAMERA CONFIG ================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ================ FLY.IO ENDPOINT ================
const char* ai_endpoint = "https://emotion.fly.dev/predict";

const char* ssid = "Meow ~";
const char* password = "nhaconuoimeo";

typedef struct __attribute__((packed)) struct_message {
  char time1[9];
  int16_t spo2;
  int16_t BPM;
  uint8_t stress;
} struct_message;
struct_message myData;

struct PendingData {
  int16_t spo2;
  int16_t bpm;
  uint8_t stress;
  char time1[9];
  bool valid = false;
} pending;

bool needToSend = false;
TFT_eSPI tft = TFT_eSPI();

String currentEmotion = "----";      // default: no face detected
float  currentConfidence = 0.0;

const char* normalMsgs[4] = {
  "You're focused and steady — keep going",
  "All is calm, you're in control",
  "Your rhythm is smooth and relaxed",
  "Keep this pace, everything is fine"
};

const char* gradualMsgs[4] = {
  "Take a slow deep breath, you got this",
  "Gently release tension from your shoulders",
  "Let your thoughts settle, you're okay",
  "Pause for a moment, inhale... exhale..."
};

const char* spikeMsgs[4] = {
  "Quick reset: breathe in slowly, exhale calmly",
  "It's fine, focus on your next step",
  "Loosen your grip on stress — one breath at a time",
  "Pause, relax, and continue with clarity"
};

const char* currentMsg = "";

camera_fb_t* takePhoto() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return nullptr;
  }
  Serial.printf("Photo captured: %d bytes\n", fb->len);
  return fb;  // caller must call esp_camera_fb_return(fb) later
}

String sendPhoto(camera_fb_t *fb) {
  if (!fb) return "";

  HTTPClient http;
  http.setTimeout(10000);
  http.begin("https://emotion.fly.dev/predict");
  http.addHeader("Content-Type", "image/jpeg");

  Serial.print("[AI] Sending... ");
  int code = http.POST(fb->buf, fb->len);

  String result = "";
  if (code == 200) {
    result = http.getString();                 // ← FULL JSON HERE!
    Serial.printf("OK → %s\n", result.c_str());
  } else {
    Serial.printf("Failed (HTTP %d)\n", code);
  }
  http.end();
  return result;  // e.g. {"emotion":"happy","confidence":0.89}
}

void sendFullData() {
  if (!pending.valid || WiFi.status() != WL_CONNECTED) return;

  String emotion = "----";
  float confidence = 0.0;

  // 1. Try to get emotion from AI
  camera_fb_t *fb = takePhoto();
  if (fb) {
    String json = sendPhoto(fb);
    esp_camera_fb_return(fb);

    if (parseEmotion(json, emotion, confidence)) {
      Serial.printf("[AI] Detected: %s (%.1f%%)\n", emotion.c_str(), confidence * 100);
    } else {
      Serial.println("[AI] No face or failed");
    }
  } else {
    Serial.println("[Camera] Capture failed");
  }

  // 2. Update screen
  currentEmotion = emotion;
  currentConfidence = confidence;
  tft.fillRect(15, 150, 210, 20, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(emotion == "----" ? TFT_DARKGREY : TFT_YELLOW);
  tft.setCursor(15, 150);
  if (emotion == "----") tft.print("Emotion: ----");
  else tft.printf("Emotion: %s - %.0f%%", emotion.c_str(), confidence * 100);

  // 3. Send everything in ONE HTTP request
  String statusStr = "No Data";
  if (pending.stress == 0) statusStr = "Normal";
  else if (pending.stress == 1) statusStr = "Gradual Stress";
  else if (pending.stress == 2) statusStr = "Stress Spike";

  String payload = "{"
    "\"spo2\":" + String(pending.spo2) +
    ",\"bpm\":" + String(pending.bpm) +
    ",\"status\":\"" + statusStr + "\"" +
    ",\"time\":\"" + String(pending.time1) + "\"" +
    ",\"emotion\":\"" + emotion + "\"" +
    ",\"confidence\":" + String(confidence, 3) +
  "}";

  HTTPClient http;
  http.setTimeout(10000);
  http.begin("https://your-app-name.onrender.com/api/data");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  Serial.printf("[Render] Full data sent → HTTP %d\n", code);
  http.end();

  // 4. Clear pending data
  pending.valid = false;
}

void centerText(const char* msg, int y, uint16_t color, uint8_t textSize=2) {
  tft.setTextSize(textSize);
  tft.setTextColor(color);

  int16_t msgWidth = tft.textWidth(msg);        // get pixel width of the string
  int16_t x = (tft.width() - msgWidth) / 2;    // center horizontally
  tft.setCursor(x, y);
  tft.println(msg);
}

bool parseEmotion(const String& json, String& emotion, float& confidence) {
  int ePos = json.indexOf("\"emotion\":\"") + 11;
  int cPos = json.indexOf("\"confidence\":") + 13;

  if (ePos < 11 || cPos < 13) return false;

  int eEnd = json.indexOf("\"", ePos);
  int cEnd = json.indexOf(",", cPos);
  if (cEnd == -1) cEnd = json.indexOf("}", cPos);

  emotion = json.substring(ePos, eEnd);
  confidence = json.substring(cPos, cEnd).toFloat();
  return true;
}

// void sendVitals(int16_t spo2, int16_t bpm, uint8_t stress, char time1[9]) {
//     if (WiFi.status() != WL_CONNECTED) return;

//     String status = "No Data";
//     if (stress == 0) status = "Normal";
//     else if (stress == 1) status = "Gradual Stress";
//     else if (stress == 2) status = "Stress Spike";

//     HTTPClient http;
//     http.setTimeout(8000);
//     http.begin("https://your-app-name.onrender.com/api/data");  // CHANGE THIS!
//     http.addHeader("Content-Type", "application/json");

//     String payload = "{\"spo2\":" + String(spo2) + 
//                  ",\"bpm\":" + String(bpm) + 
//                  ",\"status\":\"" + status + 
//                  "\",\"time\":\"" + String(time1) + "\"}";

//     int httpCode = http.POST(payload);
//     if (httpCode > 0) {
//         Serial.printf("[HTTP] POST success, code: %d\n", httpCode);
//     } else {
//         Serial.printf("[HTTP] POST failed, error: %s\n", http.getString().c_str());
//     }
//     http.end();
// }

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (len != sizeof(myData)) return;
  memcpy(&myData, data, sizeof(myData));
  pending.spo2 = myData.spo2;
  pending.bpm  = myData.BPM;
  pending.stress = myData.stress;
  strncpy(pending.time1, myData.time1, 8);
  pending.time1[8] = '\0';
  pending.valid = true;
  needToSend = true;              // ← this means: "send as soon as possible!"
  if (myData.stress == 0) {                    // NORMAL
    currentMsg = normalMsgs[random(4)];
  } else if (myData.stress == 1) {             // GRADUAL
    currentMsg = gradualMsgs[random(4)];
  } else if (myData.stress == 2) {             // SPIKE
    currentMsg = spikeMsgs[random(4)];
  } else {
    currentMsg = "Place finger on sensor";
  }

  tft.fillScreen(TFT_BLACK);

  // Time
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(12, 8);
  tft.printf("Time: %s", myData.time1);

  // BPM — big and bold
  tft.setTextSize(5);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(15, 45);
  if (myData.BPM >= 0) tft.printf("%d", myData.BPM);
  else                 tft.printf("--");
  tft.setTextSize(2);
  tft.setCursor(115, 68);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.print("BPM");

  // SpO2
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(15, 105);
  if (myData.spo2 >= 0) tft.printf("%d%%", myData.spo2);
  else                 tft.printf("--%%");
  tft.setTextSize(2);
  tft.setCursor(115, 115);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.print("SpO2");         
  // if (fb) {
  //   String jsonResponse = sendPhoto(fb);
  //   esp_camera_fb_return(fb);

  //   String emotion = "----";
  //   float confidence = 0.0;

  //   if (parseEmotion(jsonResponse, emotion, confidence)) {
  //     currentEmotion = emotion;           // update global
  //     currentConfidence = confidence;
  //     Serial.printf("[AI] Detected: %s (%.0f%%)\n", emotion.c_str(), confidence * 100);
  //   } else {
  //     // Keep previous value, just log
  //     Serial.println("[AI] No face / parse failed → keeping previous");
  //   }
  //   tft.setTextSize(2);
  //   tft.setTextColor(currentEmotion == "----" ? TFT_DARKGREY : TFT_YELLOW);
  //   tft.setCursor(15, 150);
  //   if (currentEmotion == "----") {
  //     tft.print("Emotion: ----");
  //   } else {
  //     tft.printf("Emotion: %s - %.0f%%", currentEmotion.c_str(), currentConfidence * 100);
  //   } 
  // }
  // Encouraging message — centered, calm font
  tft.setTextSize(2);
  if (myData.stress == 0)
    centerText(currentMsg, 155, tft.color565(167, 170, 225));
  else if (myData.stress == 1) 
    centerText(currentMsg, 155, tft.color565(245, 211, 196));
  else if (myData.stress == 2) 
    centerText(currentMsg, 155, tft.color565(242, 174, 187));
  else
    centerText(currentMsg, 155, TFT_WHITE);

  //sendVitals(myData.spo2, myData.BPM, myData.stress);
}

void connectWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(0,0);
  tft.println("WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    tft.print(".");
    tries++;
  }

  tft.fillScreen(TFT_BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(20, 50);
    tft.println("WiFi OK");
    Serial.println(F("WiFi connected"));
  } else {
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, 50);
    tft.println("WiFi FAIL, restarting");
    delay(2000);
    ESP.restart();
  }
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;    // 800x600
  config.jpeg_quality = 12;              // 10-63 lower = better
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 100);
    tft.println("Camera Init Failed");
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (1) delay(100);
  }
  Serial.println("Camera OK");
}

void setup() {
  Serial.begin(115200);
  randomSeed(millis());

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(40, 100);
  tft.println("Starting...");

  connectWiFi();

  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    esp_wifi_set_channel(ap.primary, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    tft.fillScreen(TFT_RED);
    tft.setCursor(30, 100);
    tft.println("ESP-NOW Fail");
    while(1);
  }

  esp_now_register_recv_cb(OnDataRecv);
  initCamera();
  if (!LittleFS.begin()) {
  Serial.println("LittleFS mount failed");
  }
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(45, 100);
  tft.println("Ready");
  delay(1500);
}

void loop() {
  static uint32_t lastFaceTime = 0;

  // ONE CALL DOES EVERYTHING: photo → AI → send full packet
  if (needToSend && pending.valid) {
    needToSend = false;
    sendFullData();           // ← This is the only thing that should run
  }

  // Emotion timeout after 45 seconds
  if (currentEmotion != "----") {
    lastFaceTime = millis();
  } else if (lastFaceTime != 0 && millis() - lastFaceTime > 45000) {
    currentEmotion = "----";
    currentConfidence = 0.0;
    lastFaceTime = 0;
    tft.fillRect(15, 150, 210, 20, TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(15, 150);
    tft.print("Emotion: ----");
  }

  delay(50);
}