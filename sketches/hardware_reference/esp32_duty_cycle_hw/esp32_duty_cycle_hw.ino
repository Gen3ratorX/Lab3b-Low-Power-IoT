// esp32_duty_cycle_hw.ino  -- REAL HARDWARE / Wokwi reference (not for Tinkercad)
// Board: ESP32 dev board (esp32 Arduino core)
// Lab 3 step 4: Wi-Fi connect, transmit, deep sleep with a timer wake-up.
// Put the INA219 in series with the board's 5 V input. MARKER_PIN is HIGH while
// Wi-Fi is connecting and LOW while transmitting, so the phases are easy to find
// in the current log. The serial output gives the time of each phase.
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_sleep.h"

const char *WIFI_SSID = "YOUR_SSID";        // fill in
const char *WIFI_PASS = "YOUR_PASSWORD";    // fill in
const IPAddress DEST_IP(192, 168, 1, 100);  // laptop running a UDP listener (e.g. nc -ul 5005)
const uint16_t DEST_PORT = 5005;
const uint64_t SLEEP_S = 60;                // 60, 600 or 3600 for the three intervals
const int MARKER_PIN = 2;

RTC_DATA_ATTR uint32_t bootCount = 0;       // survives deep sleep

void setup() {
  uint32_t tBoot = millis();
  pinMode(MARKER_PIN, OUTPUT);
  Serial.begin(115200);
  bootCount++;

  digitalWrite(MARKER_PIN, HIGH);           // phase 1: Wi-Fi connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED && millis() - tBoot < 15000) delay(10);
  uint32_t tConnected = millis();

  digitalWrite(MARKER_PIN, LOW);            // phase 2: transmit one reading
  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    WiFiUDP udp;
    udp.beginPacket(DEST_IP, DEST_PORT);
    udp.printf("boot=%lu rssi=%d\n", (unsigned long)bootCount, WiFi.RSSI());
    ok = udp.endPacket();
  }
  uint32_t tSent = millis();

  Serial.printf("boot=%lu connect_ms=%lu tx_ms=%lu ok=%d\n", (unsigned long)bootCount,
                (unsigned long)(tConnected - tBoot), (unsigned long)(tSent - tConnected), ok);
  Serial.flush();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup(SLEEP_S * 1000000ULL);   // phase 3: deep sleep
  esp_deep_sleep_start();
}

void loop() {}   // never reached: the ESP32 resets on wake-up from deep sleep
