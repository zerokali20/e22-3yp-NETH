#include <SPI.h>
#include <LoRa.h>
#include <time.h>

#define LORA_SCK_PIN 18
#define LORA_MISO_PIN 19
#define LORA_MOSI_PIN 23
#define LORA_SS_PIN 5
#define LORA_RST_PIN 14
#define LORA_DIO0_PIN 26

#define LORA_BAND 433E6

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== NETH LORA BASE GATEWAY ===");

  SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_SS_PIN);
  LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("ERROR: LoRa gateway receiver failed to start!");
    while (1);
  }

  LoRa.setSyncWord(0xF3); // Matches transmitter
  Serial.println("Gateway active. Listening for incoming telemetry...\n");
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }

    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();

    // Expected packet: EVENT,event_id,epoch_time,temperature,humidity,pressure
    if (incoming.startsWith("EVENT,")) {
      char buf[128];
      incoming.toCharArray(buf, sizeof(buf));

      char *savePtr;
      strtok_r(buf, ",", &savePtr); // "EVENT"
      char *idStr    = strtok_r(nullptr, ",", &savePtr);
      char *epochStr = strtok_r(nullptr, ",", &savePtr);
      char *tempStr  = strtok_r(nullptr, ",", &savePtr);
      char *humStr   = strtok_r(nullptr, ",", &savePtr);
      char *presStr  = strtok_r(nullptr, ",", &savePtr);

      // Convert epoch to formatted date/time string
      char timeBuffer[32] = "N/A";
      if (epochStr) {
        time_t rawTime = (time_t)strtoul(epochStr, nullptr, 10);
        struct tm *timeinfo = gmtime(&rawTime); // or localtime(&rawTime)
        if (timeinfo) {
          strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        }
      }

      Serial.println("========================================");
      Serial.printf(" [ALERT]Detection #%s\n", idStr ? idStr : "?");
      Serial.printf(" Timestamp:    %s (Epoch: %s)\n", timeBuffer, epochStr ? epochStr : "0");
      Serial.printf(" Temperature:  %s C\n", tempStr ? tempStr : "--");
      Serial.printf(" Humidity:     %s %%\n", humStr ? humStr : "--");
      Serial.printf(" Pressure:     %s hPa\n", presStr ? presStr : "--");
      Serial.printf(" Signal:       RSSI %d dBm | SNR %.2f dB\n", rssi, snr);
      Serial.println("========================================\n");
    } else {
      // Any non-standard packet fallback
      Serial.println("\n--- Raw LoRa Packet Received ---");
      Serial.printf("Data: %s\n", incoming.c_str());
      Serial.printf("RSSI: %d dBm | SNR: %.2f dB\n", rssi, snr);
      Serial.println("--------------------------------");
    }
  }
}