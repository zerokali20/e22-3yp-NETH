#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include <time.h>

// AI-Thinker ESP32-CAM pins
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

HardwareSerial sensorLink(1);

unsigned long photoNumber = 1;
bool systemReady = false;

bool initializeCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_VGA;
  c.jpeg_quality = 15;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_DRAM;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  return esp_camera_init(&c) == ESP_OK;
}

bool appendLog(unsigned long eventId,
               const char *dateTimeStr,
               unsigned long epoch,
               float temperature,
               float humidity,
               float pressure,
               const char *filename) {
  bool needsHeader = !SD_MMC.exists("/neth_log.csv");

  File file = SD_MMC.open("/neth_log.csv", FILE_APPEND);
  if (!file) {
    Serial.println("ERROR: Cannot open neth_log.csv");
    return false;
  }

  // Exactly 7 columns in header
  if (needsHeader || file.size() == 0) {
    file.println("event_id,datetime,timestamp_epoch,temperature_C,humidity_pct,pressure_hPa,photo");
  }

  // Exactly 7 values
  file.printf("%lu,%s,%lu,%.2f,%.2f,%.2f,%s\n",
              eventId, dateTimeStr, epoch, temperature, humidity, pressure, filename);

  file.close();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nNETH camera and logger starting");

  // RX on GPIO13 (GPIO14 is reserved for SD card clock)
  sensorLink.begin(9600, SERIAL_8N1, 13, -1);
  sensorLink.setTimeout(1000);

  if (!initializeCamera()) {
    Serial.println("ERROR: Camera initialization failed");
    return;
  }
  Serial.println("Camera ready");

  // Mount SD Card FIRST
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("ERROR: SD card initialization failed");
    return;
  }
  Serial.println("SD card ready");

  // Create header on fresh file after mount
  if (!SD_MMC.exists("/neth_log.csv")) {
    File file = SD_MMC.open("/neth_log.csv", FILE_WRITE);
    if (file) {
      file.println("event_id,datetime,timestamp_epoch,temperature_C,humidity_pct,pressure_hPa,photo");
      file.close();
    }
  }

  // Resume photo numbering
  while (true) {
    char name[32];
    snprintf(name, sizeof(name), "/neth_%04lu.jpg", photoNumber);
    if (!SD_MMC.exists(name)) break;
    photoNumber++;
  }

  Serial.printf("Next photo: neth_%04lu.jpg\n", photoNumber);
  Serial.println("CSV logger ready. Waiting for EVENT messages...");
  systemReady = true;
}

void loop() {
  if (!systemReady || !sensorLink.available()) {
    delay(10);
    return;
  }

  String message = sensorLink.readStringUntil('\n');
  message.trim();

  // Expected format: EVENT,event_id,epoch_time,temperature,humidity,pressure
  if (!message.startsWith("EVENT,")) {
    Serial.print("Ignored message: ");
    Serial.println(message);
    return;
  }

  unsigned long eventId = 0;
  unsigned long epochTime = 0;
  float temperature = NAN;
  float humidity = NAN;
  float pressure = NAN;

  char buffer[160];
  message.toCharArray(buffer, sizeof(buffer));

  char *savePtr;
  char *token = strtok_r(buffer, ",", &savePtr); // "EVENT"
  
  token = strtok_r(nullptr, ",", &savePtr);
  if (token) eventId = strtoul(token, nullptr, 10);
  else return;

  token = strtok_r(nullptr, ",", &savePtr);
  if (token) epochTime = strtoul(token, nullptr, 10);
  else return;

  token = strtok_r(nullptr, ",", &savePtr);
  if (token) temperature = atof(token);
  else return;

  token = strtok_r(nullptr, ",", &savePtr);
  if (token) humidity = atof(token);
  else return;

  token = strtok_r(nullptr, ",", &savePtr);
  if (token) pressure = atof(token);
  else return;

  // Convert Unix epoch to readable "YYYY-MM-DD HH:MM:SS" with UTC+5:30 offset (19800s)
  char dateTimeStr[32] = "N/A";
  if (epochTime > 0) {
    time_t rawTime = (time_t)epochTime;
    struct tm *timeinfo = gmtime(&rawTime);
    if (timeinfo) {
      strftime(dateTimeStr, sizeof(dateTimeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
    }
  }

  Serial.printf("\nMotion event #%lu received | Time: %s\n", eventId, dateTimeStr);
  Serial.printf("Temp: %.2f C | Humidity: %.2f %% | Pressure: %.2f hPa\n",
                temperature, humidity, pressure);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("ERROR: Camera returned no frame");
    return;
  }

  char filename[32];
  snprintf(filename, sizeof(filename), "/neth_%04lu.jpg", photoNumber);

  File photo = SD_MMC.open(filename, FILE_WRITE);
  if (!photo) {
    Serial.println("ERROR: Cannot create photo file");
    esp_camera_fb_return(fb);
    return;
  }

  size_t written = photo.write(fb->buf, fb->len);
  photo.close();
  esp_camera_fb_return(fb);

  if (written != fb->len) {
    Serial.printf("ERROR: Incomplete write: %s\n", filename);
    SD_MMC.remove(filename);
    return;
  }

  Serial.printf("Photo saved: %s (%u bytes)\n", filename, (unsigned int)written);

  if (appendLog(eventId, dateTimeStr, epochTime, temperature, humidity, pressure, filename)) {
    Serial.println("Event recorded in neth_log.csv");
  } else {
    Serial.println("WARNING: Photo saved, CSV log failed");
  }

  photoNumber++;
}