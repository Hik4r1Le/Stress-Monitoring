#include <TFT_eSPI.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include "esp_camera.h"
#include "config.h"

// ================ CAMERA CONFIG ================
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define MULTIPART_BUFFER_SIZE 32768



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

String currentEmotion = "----";  // default: no face detected
float currentConfidence = 0.0;
const uint32_t PHOTO_COOLDOWN = 3000;

uint8_t multipartBuffer[MULTIPART_BUFFER_SIZE];

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
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return nullptr;
  }
  Serial.printf("Photo captured: %d bytes\n", fb->len);
  return fb;  // caller must call esp_camera_fb_return(fb) later
}

// String sendPhoto(camera_fb_t* fb) {
//   if (!fb) return "";

//   HTTPClient http;
//   http.setTimeout(10000);
//   http.begin("https://emotion.fly.dev/predict");  //will change to AWS
//   http.addHeader("Content-Type", "image/jpeg");

//   Serial.print("[AI] Sending... ");
//   int code = http.POST(fb->buf, fb->len);

//   String result = "";
//   if (code == 200) {
//     result = http.getString();
//     Serial.printf("OK → %s\n", result.c_str());
//   } else {
//     Serial.printf("Failed (HTTP %d)\n", code);
//   }
//   http.end();
//   return result;  // e.g. {"emotion":"happy","confidence":0.89}
// }

String sendPhoto(camera_fb_t* fb) {
  if (!fb || fb->len == 0) {
    Serial.println("[AI] No photo to send");
    return "";
  }
  HTTPClient http;
  http.setTimeout(20000);  // Bump to 20s for AWS latency
  // Build multipart body (keep your existing code here — unchanged)
  String boundary = "----ESP32Boundary123456";
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"file\"; filename=\"photo.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "\r\n";
  tail += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n";
  tail += "esp32_cam_1\r\n";
  tail += "--" + boundary + "--\r\n";

  uint16_t headLen = head.length();
  uint16_t tailLen = tail.length();
  uint32_t totalLen = headLen + fb->len + tailLen;

  if (totalLen > MULTIPART_BUFFER_SIZE) {
    Serial.printf("[AI] Body too big: %d > %d bytes\n", totalLen, MULTIPART_BUFFER_SIZE);
    return "";
  }

  memcpy(multipartBuffer, head.c_str(), headLen);
  memcpy(multipartBuffer + headLen, fb->buf, fb->len);
  memcpy(multipartBuffer + headLen + fb->len, tail.c_str(), tailLen);

  // NEW: Debug connection before POST
  Serial.printf("[DEBUG] WiFi status: %d | IP: %s\n", WiFi.status(), WiFi.localIP().toString().c_str());
  http.begin(EMOTION_API_URL);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("User-Agent", "ESP32-CAM/1.0");  // NEW: Helps AWS accept request

  Serial.print("[AI] Sending photo to AWS... ");
  Serial.printf("%d bytes (image: %d)\n", totalLen, fb->len);

  int code = http.POST(multipartBuffer, totalLen);

  // NEW: Detailed error logging
  if (code < 0) {
    Serial.printf("[DEBUG] HTTP POST failed: %s\n", http.errorToString(code).c_str());
    if (code == HTTPC_ERROR_CONNECTION_REFUSED) Serial.println("[DEBUG] Connection refused — check AWS firewall/port 8000");
    if (code == HTTPC_ERROR_READ_TIMEOUT) Serial.println("[DEBUG] Read timeout — slow network?");
  }
 
  String result = "";
  if (code == HTTP_CODE_OK) {
    result = http.getString();
    Serial.printf("[AI] Success! → %s\n", result.c_str());
  } else {
    Serial.printf("[AI] Failed → HTTP %d\n", code);
    if (code > 0) {
      String errorBody = http.getString();
      Serial.println("[DEBUG] Server response body: " + errorBody);  // e.g., FastAPI 422 details
    }
  }
  http.end();
  return result;
}

// bool parseEmotion(const String& json, String& emotion, float& confidence) {
//   int ePos = json.indexOf("\"emotion\":\"") + 11;
//   int cPos = json.indexOf("\"confidence\":") + 13;

//   if (ePos < 11 || cPos < 13) return false;

//   int eEnd = json.indexOf("\"", ePos);
//   int cEnd = json.indexOf(",", cPos);
//   if (cEnd == -1) cEnd = json.indexOf("}", cPos);

//   emotion = json.substring(ePos, eEnd);
//   confidence = json.substring(cPos, cEnd).toFloat();
//   return true;
// }

// Parse AWS response → extract label and confidence
bool parseEmotion(const String& json, String& emotion, float& confidence) {
  if (json.length() == 0) return false;

  int labelPos = json.indexOf("\"label\":");
  int confPos = json.indexOf("\"confidence\":");

  if (labelPos == -1 || confPos == -1) return false;

  // Extract label
  int start = json.indexOf('"', labelPos + 8) + 1;
  int end = json.indexOf('"', start);
  if (start == -1 || end == -1) return false;
  emotion = json.substring(start, end);

  // Extract confidence (float)
  start = json.indexOf(':', confPos) + 1;
  end = json.indexOf(',', start);
  if (end == -1) end = json.indexOf('}', start);
  String confStr = json.substring(start, end);
  confidence = confStr.toFloat();

  return true;
}

void sendFullData() {
  if (!pending.valid || WiFi.status() != WL_CONNECTED) return;

  String emotion = "----";
  float confidence = 0.0;

  // 1. Try to get emotion from AI
  // Try 2 times 
  for (int attempt = 0; attempt < 2; attempt++) {
    camera_fb_t* fb = takePhoto();
    if (!fb) {
      Serial.println("[Camera] Capture failed");
      delay(1000);
      continue;
    }

    String json = sendPhoto(fb);
    esp_camera_fb_return(fb);

    if (parseEmotion(json, emotion, confidence) && confidence > 0.3) {  // only accept if confident
      Serial.printf("[AI] Success on attempt %d: %s (%.0f%%)\n", attempt+1, emotion.c_str(), confidence*100);
      break;
    } else {
      Serial.printf("[AI] Failed attempt %d — retrying...\n", attempt+1);
      delay(1500);
    }
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
                   "\"spo2\":"+ String(pending.spo2) 
                   + ",\"bpm\":" + String(pending.bpm) 
                   + ",\"status\":\"" + statusStr 
                   + "\"" + ",\"time\":\"" + String(pending.time1) 
                   + "\"" + ",\"emotion\":\"" + emotion 
                   + "\"" + ",\"confidence\":" + String(confidence, 3) + "}";

  HTTPClient http;
  http.setTimeout(10000);
  http.begin(RENDER_API_URL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  Serial.printf("[Render] Full data sent → HTTP %d\n", code);
  http.end();

  // 4. Clear pending data
  pending.valid = false;
}

// void centerText(const char* msg, int y, uint16_t color, uint8_t textSize = 2) {
//   tft.setTextSize(textSize);
//   tft.setTextColor(color);
//   tft.setTextWrap(false);

//   int16_t msgWidth = tft.textWidth(msg);     // get pixel width of the string
//   int16_t x = (tft.width() - msgWidth) / 2;  // center horizontally
//   tft.setCursor(x, y);
//   tft.println(msg);
// }

void centerText(const char* msg, int y, uint16_t color, uint8_t textSize = 2) {
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  tft.setTextWrap(false);

  String s = String(msg);
  int16_t maxWidth = tft.width() - 20;
  int16_t charWidth = tft.textWidth("M"); 
  if (tft.textWidth(s.c_str()) <= maxWidth) {
    int16_t x = (tft.width() - tft.textWidth(s.c_str())) / 2;
    tft.setCursor(x, y);
    tft.println(s);
    return;
  }

  // Long text → split into 2 lines
  int splitPos = s.length() * 0.55;  // try to split a bit earlier
  while (splitPos > 0 && tft.textWidth(s.substring(0, splitPos).c_str()) > maxWidth)
    splitPos--;

  String line1 = s.substring(0, splitPos);
  String line2 = s.substring(splitPos);

  // Center each line
  int16_t x1 = (tft.width() - tft.textWidth(line1.c_str())) / 2;
  int16_t x2 = (tft.width() - tft.textWidth(line2.c_str())) / 2;

  tft.setCursor(x1, y);
  tft.println(line1);
  tft.setCursor(x2, y + (textSize * 8) + 4);
  tft.println(line2);
}

// void sendVitals(int16_t spo2, int16_t bpm, uint8_t stress, char time1[9]) {
//     if (WiFi.status() != WL_CONNECTED) return;

//     String status = "No Data";
//     if (stress == 0) status = "Normal";
//     else if (stress == 1) status = "Gradual Stress";
//     else if (stress == 2) status = "Stress Spike";

//     HTTPClient http;
//     http.setTimeout(8000);
//     http.begin("https://your-app-name.onrender.com/api/data");
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

void OnDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
  if (len != sizeof(myData)) return;
  memcpy(&myData, data, sizeof(myData));
  pending.spo2 = myData.spo2;
  pending.bpm = myData.BPM;
  pending.stress = myData.stress;
  strncpy(pending.time1, myData.time1, 8);
  pending.time1[8] = '\0';
  pending.valid = true;
  needToSend = true;

  if (myData.stress == 0) {  // NORMAL
    currentMsg = normalMsgs[random(4)];
  } else if (myData.stress == 1) {  // GRADUAL
    currentMsg = gradualMsgs[random(4)];
  } else if (myData.stress == 2) {  // SPIKE
    currentMsg = spikeMsgs[random(4)];
  } else {
    currentMsg = " Adjust your sensor to read";
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
  else tft.printf("--");
  tft.setTextSize(2);
  tft.setCursor(115, 68);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.print("BPM");

  // SpO2
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(15, 105);
  if (myData.spo2 >= 0) tft.printf("%d%%", myData.spo2);
  else tft.printf("--%%");
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
    centerText(currentMsg, 180, tft.color565(167, 170, 225));
  else if (myData.stress == 1)
    centerText(currentMsg, 180, tft.color565(245, 211, 196));
  else if (myData.stress == 2)
    centerText(currentMsg, 180, tft.color565(242, 174, 187));
  else
    centerText(currentMsg, 155, TFT_WHITE);

  //sendVitals(myData.spo2, myData.BPM, myData.stress);
}

void connectWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
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
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;       // 10 MHz
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_QVGA;  
  config.jpeg_quality = 12;
  config.fb_count     = 1;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  const int MAX_RETRIES = 10;
  
  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    Serial.printf("Camera init attempt %d/%d...\n", attempt, MAX_RETRIES);
    //esp_brownout_disable();
    
    esp_err_t err = esp_camera_init(&config);
    
    if (err == ESP_OK) {
      Serial.println("Camera initialized successfully!");
      sensor_t *s = esp_camera_sensor_get();
      s->set_framesize(s, FRAMESIZE_VGA);  //640x480
      Serial.println("Resolution upgraded to VGA");
      return;
    }
    
    Serial.printf("Attempt %d failed: 0x%x\n", attempt, err);
    delay(500);
  }

  // All retries failed = show error + auto-restart
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 80);
  tft.setTextSize(2);
  tft.println("Camera failed");
  tft.setCursor(10, 120);
  tft.println("Restarting...");
  Serial.println("Camera failed after all retries → restarting ESP32-CAM");
  delay(2000);
  ESP.restart();  // ← will try again from boot → infinite retry until it works
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
    while (1)
      ;
  }

  esp_now_register_recv_cb(OnDataRecv);
  initCamera();
  // if (!LittleFS.begin()) {
  //   Serial.println("LittleFS mount failed");
  // }
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(45, 100);
  tft.println("Ready");
  delay(1500);
}

void loop() {
  static uint32_t lastFaceTime = 0;
  static uint32_t lastPhotoTime = 0;
  //photo -> AI -> return to the device -> send full packet to Render
  if (needToSend && pending.valid && (millis() - lastPhotoTime > PHOTO_COOLDOWN)) {
    needToSend = false;
    lastPhotoTime = millis();
    
    sendFullData();
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