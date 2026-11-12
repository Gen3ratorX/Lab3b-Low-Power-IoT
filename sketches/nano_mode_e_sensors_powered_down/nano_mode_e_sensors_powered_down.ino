// nano_mode_e_sensors_powered_down.ino
// Profile: Nano 33 BLE Sense node (emulated on Uno)
// Mode e: Idle in loop() with sensors powered down (library end() calls)
// Paste this whole file into the Tinkercad Code panel (Text mode), start the
// simulation, open the Serial Monitor, and read the multimeter at the same time.
// Every number this sketch prints comes from the simulation. Write down what you see.
// =====================================================================
//  Lab 3b (L03b) - Power measurement and battery life, Tinkercad version
//  Board in Tinkercad: Arduino Uno R3 (emulates the node's loads)
//  "Software INA219": a 22 ohm shunt in the node's ground return,
//  read on A0. The Tinkercad multimeter (current mode) sits in series
//  with the same ground return as an independent check.
// =====================================================================

// ---------- Pin map (must match the wiring table in the write-up) ----
const uint8_t PIN_SENS_CONV = 3;  // "sensor conversion" load: 3.3 kohm
const uint8_t PIN_MCU       = 4;  // "MCU active" load: 1.5 kohm
const uint8_t PIN_RADIO     = 5;  // "radio TX" load: 750 ohm
const uint8_t PIN_MIC       = 6;  // "PDM microphone" load: 4.7 kohm
const uint8_t PIN_SENS_PWR  = 7;  // sensor supply: TMP36 +Vs and 39 k + 10 k pot
const uint8_t PIN_BOARD     = 8;  // "board overhead" (power LED): green LED + 4.7 kohm
const uint8_t PIN_SHUNT     = A0; // top of the 22 ohm shunt (= node ground rail)
const uint8_t PIN_TMP36     = A1; // TMP36 Vout
const uint8_t PIN_HUM       = A2; // potentiometer wiper = stand-in humidity sensor

// ---------- Measurement settings --------------------------------------
// 1 = use the ATmega328P internal 1.1 V ADC reference (0.049 mA per step
// with a 22 ohm shunt). If the sketch's current disagrees with the
// Tinkercad multimeter by a factor of about 4.5, set this to 0
// (5 V reference, about 0.22 mA per step).
#define USE_INTERNAL_REF 1
#define BOARD_LED_OVERHEAD 0        // 1 = keep the "power LED" load on in every state
const float SHUNT_OHMS = 22.0;
#if USE_INTERNAL_REF
const float VREF = 1.1;
#else
const float VREF = 5.0;
#endif
const uint8_t ADC_AVG = 8;

float readPin_mV(uint8_t pin) {
  analogRead(pin);                  // discard first reading after a channel change
  long s = 0;
  for (uint8_t i = 0; i < ADC_AVG; i++) s += analogRead(pin);
  return (s / (float)ADC_AVG) * VREF * 1000.0 / 1024.0;
}

float readCurrent_mA() { return readPin_mV(PIN_SHUNT) / SHUNT_OHMS; }

// TMP36: 0.5 V at 0 degC, 10 mV/degC, measured relative to the node ground rail
float readTempC() {
  float v = readPin_mV(PIN_TMP36) - readPin_mV(PIN_SHUNT);
  return (v - 500.0) / 10.0;
}

// Pot through 39 k from the sensor supply: wiper spans about 0 to 1.02 V.
// Mapped linearly to 0-100 %RH. This is a stand-in, not a humidity sensor.
float readHumidity() {
  float v = readPin_mV(PIN_HUM) - readPin_mV(PIN_SHUNT);
  float rh = v / 1020.0 * 100.0;
  if (rh < 0) rh = 0;
  if (rh > 100) rh = 100;
  return rh;
}

void setLoads(bool mcu, bool sensPwr, bool sensConv, bool mic, bool radio) {
  digitalWrite(PIN_MCU, mcu);
  digitalWrite(PIN_SENS_PWR, sensPwr);
  digitalWrite(PIN_SENS_CONV, sensConv);
  digitalWrite(PIN_MIC, mic);
  digitalWrite(PIN_RADIO, radio);
  digitalWrite(PIN_BOARD, BOARD_LED_OVERHEAD ? HIGH : LOW);
}

void setupPins() {
  uint8_t outs[] = {PIN_SENS_CONV, PIN_MCU, PIN_RADIO, PIN_MIC, PIN_SENS_PWR, PIN_BOARD};
  for (uint8_t i = 0; i < sizeof(outs); i++) { pinMode(outs[i], OUTPUT); digitalWrite(outs[i], LOW); }
#if USE_INTERNAL_REF
  analogReference(INTERNAL);
#else
  analogReference(DEFAULT);
#endif
  analogRead(PIN_SHUNT);
  delay(5);
}

// ---------- Charge integrator (what the INA219 logger would do) --------
// Written as a struct with member functions so the Arduino IDE and
// Tinkercad's automatic prototypes do not trip over it.
struct Meter {
  unsigned long tStart, tLast;   // micros()
  double q_mAms;                 // integrated charge in mA*ms
  float iMin, iMax;
  unsigned long n;

  void start() { tStart = tLast = micros(); q_mAms = 0; iMin = 1e9; iMax = 0; n = 0; }

  float sample() {
    float i = readCurrent_mA();
    unsigned long now = micros();
    q_mAms += i * ((now - tLast) / 1000.0);   // charge += current x time
    tLast = now;
    if (i < iMin) iMin = i;
    if (i > iMax) iMax = i;
    n++;
    return i;
  }

  float elapsed_ms() { return (tLast - tStart) / 1000.0; }
  float charge_mC()  { return q_mAms / 1000.0; }          // mA*s = mC
  float avg_mA()     { float t = elapsed_ms(); return t > 0 ? (float)(q_mAms / t) : 0; }

  // scale = 1 for the Nano profile, 20 for the ESP32 profile (1 sim mA = 20 real mA)
  void print(const char *label, float scale) {
    Serial.print(label);
    Serial.print(F(", t_ms="));      Serial.print(elapsed_ms(), 1);
    Serial.print(F(", I_avg_mA="));  Serial.print(avg_mA() * scale, 3);
    Serial.print(F(", I_max_mA="));  Serial.print(iMax * scale, 3);
    Serial.print(F(", Q_mC="));      Serial.print(charge_mC() * scale, 3);
    Serial.print(F(", Q_uAh="));     Serial.print(charge_mC() * scale / 3.6, 4);
    Serial.print(F(", samples="));   Serial.println(n);
  }
};

const float SCALE = 1;   // Nano profile: 1 sim mA = 1 mA

void setup() {
  Serial.begin(9600);
  setupPins();
  setLoads(1,0,0,0,0);
  Serial.println(F("nano_mode_e_sensors_powered_down: Idle in loop() with sensors powered down (library end() calls)"));
  Serial.println(F("Each line = one 1 s window of the shunt logger."));
}

void loop() {
  Meter m;
  m.start();
  unsigned long t0 = millis();
  unsigned long lastRead = 0;
  while (millis() - t0 < 1000) {
    // nothing else to do in this mode
    m.sample();
    delay(5);
  }
  m.print("MODE e", SCALE);

}
