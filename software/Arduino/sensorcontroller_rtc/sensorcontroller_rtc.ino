#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "RTClib.h"

// --- Sensors & Camera UART Pins ---
#define PIR_PIN 27
#define SDA_PIN 21
#define SCL_PIN 22
#define CAM_TX_PIN 17

// --- SX1278 LoRa SPI Pins ---
#define LORA_SCK_PIN 18
#define LORA_MISO_PIN 19
#define LORA_MOSI_PIN 23
#define LORA_SS_PIN 5
#define LORA_RST_PIN 14
#define LORA_DIO0_PIN 26

#define LORA_BAND 433E6
#define CAPTURE_COOLDOWN_MS 10000UL

Adafruit_BME280 bme;
RTC_DS3231 rtc;

bool bmeReady = false;
bool rtcReady = false;
bool loraReady = false;
bool previousMotion = false;
bool captureHasOccurred = false;

unsigned long lastCaptureTime = 0;
unsigned long eventCounter = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== NETH WILDLIFE MONITOR (RTC + BME + LORA) ===");

  // 1. UART to ESP32-CAM
  Serial2.begin(9600, SERIAL_8N1, -1, CAM_TX_PIN);

  // 2. Shared I2C bus initialization
  pinMode(PIR_PIN, INPUT);
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize RTC
  if (rtc.begin(&Wire)) {
    rtcReady = true;
    Serial.println("DS3231 RTC ready");
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting time to compile time!");
      //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  } else {
    Serial.println("WARNING: DS3231 RTC not detected!");
  }

  // Initialize BME280
  if (bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire)) {
    bmeReady = true;
    Serial.println("BME280 ready");
  } else {
    Serial.println("WARNING: BME280 not detected");
  }

  // 3. Initialize LoRa
  SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_SS_PIN);
  LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  if (LoRa.begin(LORA_BAND)) {
    loraReady = true;
    LoRa.setSyncWord(0xF3);
    Serial.println("LoRa SX1278 ready");
  }

  Serial.println("Stabilizing PIR (30s)...");
  delay(30000);
  previousMotion = (digitalRead(PIR_PIN) == HIGH);
  Serial.println("System armed.");
}

void loop() {
  bool motion = (digitalRead(PIR_PIN) == HIGH);
  unsigned long now = millis();

  bool newMotionEvent = motion && !previousMotion;

  if (newMotionEvent) {
    bool cooldownOver = !captureHasOccurred || (unsigned long)(now - lastCaptureTime) >= CAPTURE_COOLDOWN_MS;

    if (cooldownOver) {
      eventCounter++;

      // Timestamp from RTC (falls back to millis if RTC fails)
      uint32_t epochTime = 0;
      if (rtcReady) {
        DateTime dt = rtc.now();
        epochTime = dt.unixtime();
      } else {
        epochTime = now / 1000;
      }

      float temp = bmeReady ? bme.readTemperature() : NAN;
      float hum  = bmeReady ? bme.readHumidity() : NAN;
      float pres = bmeReady ? (bme.readPressure() / 100.0F) : NAN;

      // New standard event packet: EVENT,id,epoch_time,temp,hum,pres
      char payload[128];
      snprintf(payload, sizeof(payload), "EVENT,%lu,%lu,%.2f,%.2f,%.2f",
               eventCounter, (unsigned long)epochTime, temp, hum, pres);

      // Send to Camera
      Serial2.println(payload);

      // Send over LoRa
      if (loraReady) {
        LoRa.beginPacket();
        LoRa.print(payload);
        LoRa.endPacket();
      }

      Serial.printf("Dispatched: %s\n", payload);
      lastCaptureTime = now;
      captureHasOccurred = true;
    }
  }

  previousMotion = motion;
  delay(50);
}