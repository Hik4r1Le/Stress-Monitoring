#include <WiFi.h>
#include "time.h"
#include <Wire.h>
#include "MAX30105.h"          // SparkFun MAX30102 library
#include "spo2_algorithm.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ==================== CONFIG ====================
const char* ssid      = "Meow ~";
const char* password  = "nhaconuoimeo";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;   // UTC+7
const int   daylightOffset_sec = 0;

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define I2C_SDA 8
#define I2C_SCL 10
MAX30105 particleSensor;

#define BUFFER_SIZE 50
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

// ------------------- ESP-NOW -------------------
uint8_t peerAddress[] = {0x6C,0xC8,0x40,0x33,0x4D,0x54};   
esp_now_peer_info_t peerInfo;

// ------------------- Data packet ----------------
typedef struct __attribute__((packed)) struct_message {
  char time1[9];   // HH:MM:SS + '\0'
  int16_t  spo2;
  int16_t  BPM;
  uint8_t stress;
} struct_message;

struct_message myData;

// ------------------- Globals -------------------
int  spo2 = 0, heartRate = 0;
String time0 = "00:00:00";
bool valid = false; //data validation

const unsigned long INTERVAL = 1000; //data collection interval, short for testing
unsigned long lastRead = 0;

bool baselineReady = false;
float baselineHR = 0;
float bSigma = 0;
float HR_filtered = 0.0;
const float alpha = 0.20;

const int BASELINE_WINDOW = 2; //for testing only          
const int BASELINE_READS = BASELINE_WINDOW; 
float baselineArr[BASELINE_WINDOW];
int baselineCount = 0;
float spikeThreshold = 0.0;
float gradualThreshold = 0.0;
const unsigned long BASELINE_INTERVAL = 100; 
unsigned long currentInterval = BASELINE_INTERVAL;

enum State { BASELINING, NORMAL };
State systemState = BASELINING;
String currentStress = "NORMAL";

// const int VIB_PIN = 4; 
// unsigned long vibStartTime = 0;
// int vibDuration = 0;
// bool vibrating = false;
// const int VIB_FREQ = 500;
// const int VIB_RESOLUTION = 8;

// ==================== HELPERS ====================

/* ---- Display ---- */
void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 init failed"));
    for (;;) delay(100);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
}

/* ---- Wi-Fi ---- */
void connectWiFi() {
  display.clearDisplay();
  display.setCursor(0,0); display.println(F("WiFi...")); display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    display.print("."); display.display();
    tries++;
  }

  display.clearDisplay();
  if (WiFi.status() == WL_CONNECTED) {
    display.println(F("WiFi OK"));
    Serial.println(F("WiFi connected"));
  } else {
    display.println(F("WiFi FAIL, restarting"));
    display.display();
    delay(2000);
    ESP.restart();
  }
  display.display();
  delay(1000);
}

/* ---- NTP ---- */
void initNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

/* ---- MAX30102 ---- */
void initSensor() {
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println(F("MAX30102 not found"));
    display.clearDisplay(); display.setCursor(0,0);
    display.println(F("MAX30102 missing!")); display.display();
    for (;;) delay(100);
  }
  byte ledBrightness = 0x0F;     
  byte sampleAverage = 16;        
  byte ledMode = 2;              // Red + IR
  int sampleRate = 100;          // 100 Hz
  int pulseWidth = 411;          // µs
  int adcRange = 4096;

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(15);   
  particleSensor.setPulseAmplitudeIR(64);

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Sensor warming...");
  display.println("Please wait 8s");
  display.display();
  delay(8000);
  display.setCursor(0,20);
  display.println(F("Sensor ready..."));
  display.display();
}

/* ---- ESP-NOW ---- */
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.printf("ESP-NOW send: %s\n",
                (status == ESP_NOW_SEND_SUCCESS) ? "OK" : "FAIL");
}

void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println(F("ESP-NOW init error"));
    for (;;) delay(100);
  }
  Serial.println("ESP-NOW initialized!");  

  esp_now_register_send_cb(OnDataSent);
  //get wifi channel
  uint8_t primary;   
  wifi_second_chan_t second;
  esp_wifi_get_channel(&primary, &second);
  Serial.printf("Using WiFi channel: %d\n", primary);

  // Add peer with channel above
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = primary;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println(F("Failed to add peer"));
    for (;;) delay(100);
  }
  else {
    Serial.println("Peer added!");                                           
  }
}

/* ---- Time ---- */
String getTimeString() {
  struct tm ti;
  if (!getLocalTime(&ti)) return "--:--:--";
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
  time0 = String(buf);
  return time0;
}

/* ---- Sensor read ---- */
void readMAX() {
  int i = 0;

  while (i < BUFFER_SIZE) {
    //update internal state of the library
    particleSensor.check(); // will fill internal FIFO and set flag
    if (particleSensor.available()) {
      uint32_t ir = particleSensor.getIR();
      uint32_t red = particleSensor.getRed();

      irBuffer[i] = ir;
      redBuffer[i] = red;

      particleSensor.nextSample();

      i++;
    } else {
      delayMicroseconds(1000);
    }
  }
  delay(1);
}

/* ---- Vitals calculation ---- */
void calculateVitals() {
  int32_t spo2_raw, hr_raw;
  int8_t  vspo2_raw, vhr_raw;

  maxim_heart_rate_and_oxygen_saturation(
      irBuffer, BUFFER_SIZE, redBuffer,
      &spo2_raw, &vspo2_raw, &hr_raw, &vhr_raw);

  spo2          = (int)spo2_raw;
  heartRate     = (int)hr_raw;
  //limit
  const int MIN_HR = 30;
  const int MAX_HR = 220;
  const int MIN_SPO2 = 70;
  const int MAX_SPO2 = 100;
  valid = true;
  //validation
  if (heartRate < MIN_HR || heartRate > MAX_HR) {
    valid = false;
    heartRate = 0;
  }

  if (spo2 < MIN_SPO2 || spo2 > MAX_SPO2) {
    valid = false;
    spo2 = 0;
  }

  String currentTime = getTimeString();  // updates time0
  if (currentTime == "--:--:--" || time0.indexOf('-') != -1) {
    time0 = "00:00:00";  // fallback
  }
  Serial.printf("Raw HR: %d valid: %d | SpO2: %d valid: %d\n",
              hr_raw, vhr_raw, spo2_raw, vspo2_raw);
              Serial.print("RED = ");
  Serial.print(particleSensor.getRed());
  Serial.print("  IR = ");
  Serial.println(particleSensor.getIR());
//   // ===== AC/DC DEBUG =====
// static uint32_t r_max = 0, r_min = 0xFFFFFFFF;
// static uint32_t i_max = 0, i_min = 0xFFFFFFFF;

// // scan the BUFFER
// r_max = r_min = redBuffer[0];
// i_max = i_min = irBuffer[0];

// for (int i = 1; i < BUFFER_SIZE; i++) {
//   if (redBuffer[i] > r_max) r_max = redBuffer[i];
//   if (redBuffer[i] < r_min) r_min = redBuffer[i];
//   if (irBuffer[i] > i_max)  i_max  = irBuffer[i];
//   if (irBuffer[i] < i_min)  i_min  = irBuffer[i];
// }

// uint32_t redDC = (r_max + r_min) / 2;
// uint32_t irDC  = (i_max + i_min) / 2;
// uint32_t redAC = r_max - r_min;
// uint32_t irAC  = i_max - i_min;

// Serial.printf("DC/AC → RED_DC=%lu RED_AC=%lu | IR_DC=%lu IR_AC=%lu\n",
//                redDC, redAC, irDC, irAC);
  if (!valid) {
    Serial.println("Invalid data - skip sending");
  }
}

void collectBaseline(float hr) {
  if (baselineReady) return;

  if (baselineCount == 0) {
    HR_filtered = hr;
  } else {
    HR_filtered = alpha * hr + (1 - alpha) * HR_filtered;
  }

  baselineArr[baselineCount] = HR_filtered;
  baselineCount++;

  // --- Update display ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Sit still..."));
  display.printf("Baseline: %d/%d\n", baselineCount, BASELINE_WINDOW);
  display.printf("HR: %.1f\n", HR_filtered);
  display.display();

  Serial.printf("Baseline %d/%d → HR=%.1f\n", baselineCount, BASELINE_WINDOW, HR_filtered);

  if (baselineCount >= BASELINE_WINDOW) {
    // --- Compute mean ---
    float sum = 0;
    for (int i = 0; i < BASELINE_WINDOW; i++) sum += baselineArr[i];
    baselineHR = sum / BASELINE_WINDOW;

    // --- Compute sigma ---
    float var = 0;
    for (int i = 0; i < BASELINE_WINDOW; i++) {
      float d = baselineArr[i] - baselineHR;
      var += d * d;
    }
    bSigma = sqrt(var / BASELINE_WINDOW);

    // --- Thresholds ---
    spikeThreshold = baselineHR + 3.0 * bSigma;
    gradualThreshold = baselineHR + 1.4 * bSigma;

    baselineReady = true;
    systemState = NORMAL;
    currentInterval = INTERVAL;

    // --- Final message ---
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("BASELINE READY!"));
    display.printf("HR: %.1f ± %.1f\n", baselineHR, bSigma);
    display.printf("Spike > %.1f\n", spikeThreshold);
    display.display();
    delay(2000);

    Serial.println("===== BASELINE READY =====");
    Serial.printf("HR=%.1f sigma=%.1f Spike=%.1f Gradual=%.1f\n",
                  baselineHR, bSigma, spikeThreshold, gradualThreshold);
  }
}

String detectStress(float hrFilteredNow) {
  if (!baselineReady) return "BASELINING";

  if (hrFilteredNow > spikeThreshold) return "SPIKE";

  if (hrFilteredNow > gradualThreshold) return "GRADUAL";

  return "NORMAL";
}

// void updateVibration() {
//   if (vibrating) {
//     if (millis() - vibStartTime >= vibDuration) {
//       ledcWrite(VIB_PIN, 0);  // Stop vibration
//       vibrating = false;
//       Serial.println("Vibration stopped");
//     }
//   }
// }

void computeFHR() {
  if (!valid || !baselineReady) {
    currentStress = "INVALID";
    return;
  }
  HR_filtered = alpha * heartRate + (1 - alpha) * HR_filtered;
  currentStress = detectStress(HR_filtered);

  // Vibration logic
  // if (currentStress == "GRADUAL" && !vibrating) {
  //   ledcWrite(VIB_PIN, 128);  
  //   vibDuration = 2000;      
  //   vibStartTime = millis();
  //   vibrating = true;
  //   Serial.println("GRADUAL vibration");
  // } else if (currentStress == "SPIKE" && !vibrating) {
  //   ledcWrite(VIB_PIN, 255);  
  //   vibDuration = 3000;     
  //   vibStartTime = millis();
  //   vibrating = true;
  //   Serial.println("SPIKE vibration");
  // }
  Serial.printf("Filtered HR: %.1f → %s\n", HR_filtered, currentStress.c_str());
}

/* ---- ESP-NOW transmit ---- */
void sendData() {
  // Fill packet
  time0.toCharArray(myData.time1, 9);   // includes '\0'
  myData.spo2 = valid ? spo2 : -1;
  if (valid && baselineReady) {
    myData.BPM = (int)(HR_filtered + 0.5f);
    if (currentStress == "SPIKE")      myData.stress = 2;
    else if (currentStress == "GRADUAL") myData.stress = 1;
    else                                myData.stress = 0;
  } else {
    myData.BPM = -1;
    myData.stress = 255;
  }
  esp_err_t res;
  int retries = 0;
  do {
    res = esp_now_send(peerAddress, (uint8_t*)&myData, sizeof(myData));
    if (res == ESP_OK) {
      Serial.println("ESP-NOW sent (OK)");
      return;
    }
    retries++;
    delay(10);  // small backoff
  } while (retries < 3);
  Serial.println("ESP-NOW send FAILED after retries");
}

/* ---- OLED update ---- */
void updateDisplay() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0,0);
  display.print(F("Time: "));
  display.println(getTimeString());

  display.setTextSize(2); 
  display.setCursor(0,12); 
  display.print(F("BPM:"));
  display.setTextSize(3); 
  display.setCursor(60,12);
  if (valid && baselineReady)
    display.println(String((int)(HR_filtered)));
  else
    display.println("--");

  display.setTextSize(2); 
  display.setCursor(0,44); 
  display.print(F("SpO2:"));
  display.setCursor(70,44);
  display.println(valid ? String(spo2)+"%" : "--%");

  // display.setTextSize(1); 
  // display.setCursor(0,56);
  // display.print(F("State: ")); 
  // display.print(currentStress);

  display.display();
}

// ==================== SETUP / LOOP ====================

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  initDisplay();
  connectWiFi();
  wifi_ap_record_t ap_info;
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
  uint8_t apChannel = ap_info.primary;
  esp_wifi_set_channel(apChannel, WIFI_SECOND_CHAN_NONE);
  delay(100);
  Serial.printf("SYNCED TO AP CHANNEL %d\n", apChannel);
} else {
  Serial.println("Failed to get AP channel - using current");
}     
  initNTP();
  initESPNow();       
  initSensor();
  // ledcAttach(VIB_PIN, 500, 8);  // Pin, freq 500Hz, 8-bit res
  // ledcWrite(VIB_PIN, 0);
  Serial.println("Vibration motor ready");
}

void loop() {
  if (millis() - lastRead >= currentInterval) {
    lastRead = millis();

    readMAX();
    calculateVitals();

    if (valid) {
      if (systemState == BASELINING) {
        collectBaseline(heartRate);
      } else {
        computeFHR();  // EMA + stress
        updateDisplay();
        Serial.printf("HR:%d bpm | SpO2:%d%% | %s\n",
                heartRate, spo2, time0.c_str());
        sendData();
        //updateVibration();
      }
    } else {
      Serial.println("Invalid reading - waiting...");
      if (systemState == BASELINING) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(F("Sit still..."));
        display.printf("Baseline: %d/%d\n", baselineCount, BASELINE_WINDOW);
        display.println("Bad signal, retry");
        display.display();
      } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 20);
        display.println("No finger / bad signal");
        display.display();
      }
    }
  }
}