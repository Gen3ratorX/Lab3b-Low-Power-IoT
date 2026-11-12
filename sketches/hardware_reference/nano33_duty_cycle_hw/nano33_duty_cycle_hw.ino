// nano33_duty_cycle_hw.ino  -- REAL HARDWARE reference (not for Tinkercad)
// Board: Arduino Nano 33 BLE Sense Rev2 (Arduino Mbed OS Nano Boards core)
// Libraries: ArduinoBLE, Arduino_HS300x (Rev2 temperature/humidity sensor HS3003)
// Lab 3 step 3: wake every 60 s, read temperature and humidity, advertise for
// 2 s, then idle. Measure the supply current with the INA219 in series on VIN.
// MARKER_PIN goes HIGH during the active part of the cycle so you can line up
// the INA219 log with the code (connect it to a logger input if you have one).
#include <ArduinoBLE.h>
#include <Arduino_HS300x.h>

const unsigned long CYCLE_MS = 60000UL;
const unsigned long ADV_MS   = 2000UL;
const int MARKER_PIN = 2;
const bool USE_SERIAL = false;   // true only while debugging on USB; Serial costs current

void setup() {
  pinMode(MARKER_PIN, OUTPUT);
  digitalWrite(MARKER_PIN, LOW);
  pinMode(LED_PWR, OUTPUT);
  digitalWrite(LED_PWR, LOW);    // switch off the power LED (board overhead)
  if (USE_SERIAL) Serial.begin(115200);
}

void loop() {
  unsigned long t0 = millis();
  digitalWrite(MARKER_PIN, HIGH);

  // 1) read temperature and humidity, then power the sensor library down
  float t = NAN, h = NAN;
  if (HS300x.begin()) {
    t = HS300x.readTemperature();
    h = HS300x.readHumidity();
    HS300x.end();
  }

  // 2) advertise the reading for 2 s (manufacturer data: 0xFFFF test ID, T*100, RH*100)
  if (BLE.begin()) {
    int16_t t100 = isnan(t) ? 0 : (int16_t)(t * 100);
    uint16_t h100 = isnan(h) ? 0 : (uint16_t)(h * 100);
    uint8_t md[6] = {0xFF, 0xFF, (uint8_t)(t100 & 0xFF), (uint8_t)(t100 >> 8),
                     (uint8_t)(h100 & 0xFF), (uint8_t)(h100 >> 8)};
    BLE.setLocalName("AIoT-Node");
    BLE.setManufacturerData(md, sizeof(md));
    BLE.advertise();
    unsigned long ta = millis();
    while (millis() - ta < ADV_MS) BLE.poll();
    BLE.stopAdvertise();
    BLE.end();
  }
  digitalWrite(MARKER_PIN, LOW);

  if (USE_SERIAL) {
    Serial.print("T_C="); Serial.print(t, 2);
    Serial.print(" RH="); Serial.print(h, 1);
    Serial.print(" active_ms="); Serial.println(millis() - t0);
  }

  // 3) idle until the next wake-up (Mbed OS sleeps the CPU inside delay())
  unsigned long used = millis() - t0;
  if (used < CYCLE_MS) delay(CYCLE_MS - used);
}
