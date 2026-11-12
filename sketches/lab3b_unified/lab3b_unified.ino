// lab3b_unified.ino
// Lab 3b (L03b) - ONE sketch that runs the whole Tinkercad lab in a single simulation:
//   Part 1  Nano node modes a-f, about 1 s each, one line per mode   (= nano_mode_a..f / nano_mode_sweep)
//   Part 2  Nano duty cycle: WAKE+SENSE -> ADVERTISE -> SLEEP       (= nano_duty_cycle)
//   Part 3  Summary table, charge shares, and battery-life estimates at 1, 10 and 60 min
//           at 2600 mAh nominal and at the derated 1635 mAh (5 V side).
// Load patterns (which pins are on in each state), state times and sample rates are the
// same as in the individual sketches, so the numbers should match them.
// Same circuit as the write-up (B4): Arduino Uno R3, 22 ohm shunt on A0, internal 1.1 V reference.
// No external libraries. Paste the whole file into Tinkercad Code -> Text.
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

// Functions below take void* instead of Meter/DutyResult because Tinkercad puts its
// automatic prototypes at the top of the file, before these types exist.
float readCurrent_mA();   // defined below, used by Meter

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

  // scale = 1 for the Nano profile (1 sim mA = 1 real mA)
  void print(const char *label, float scale) {
    Serial.print(label);
    Serial.print(F(", t_ms="));      Serial.print(elapsed_ms(), 1);
    Serial.print(F(", I_avg_mA="));  Serial.print(avg_mA() * scale, 3);
    Serial.print(F(", I_max_mA="));  Serial.print(iMax * scale, 3);
    Serial.print(F(", Q_mC="));      Serial.print(charge_mC() * scale, 3);
    Serial.print(F(", Q_uAh="));     Serial.print(charge_mC() * scale / 3.6, 4);
    Serial.print(F(", samples="));   Serial.println(n);
  }

  // Added for the unified sketch: same output, but the label stays in flash (F("...")) to save RAM.
  void print(const __FlashStringHelper *label, float scale) {
    Serial.print(label);
    Serial.print(F(", t_ms="));      Serial.print(elapsed_ms(), 1);
    Serial.print(F(", I_avg_mA="));  Serial.print(avg_mA() * scale, 3);
    Serial.print(F(", I_max_mA="));  Serial.print(iMax * scale, 3);
    Serial.print(F(", Q_mC="));      Serial.print(charge_mC() * scale, 3);
    Serial.print(F(", Q_uAh="));     Serial.print(charge_mC() * scale / 3.6, 4);
    Serial.print(F(", samples="));   Serial.println(n);
  }
};

// Results of one duty cycle, kept for the summary
struct DutyResult {
  float tState_ms[2];   // duration of each active state (WAKE+SENSE, ADVERTISE)
  float iState_mA[2];   // average current of each active state (real mA, scaled)
  float qState_mC[2];   // charge of each active state (real mC, scaled)
  uint8_t nStates;
  float qActive_mC, tActive_ms;
  float tSleep_ms, iSleepMeas_mA, iSleep_mA;   // iSleep_mA = value used for estimates
  float tCycle_ms, iCycle_mA, qCycle_mC;
  float tempC, rh;
};

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


// =====================================================================
//  Settings for the unified run
// =====================================================================
// Length of ONE Nano duty cycle. The lab procedure uses 60000UL (wake every 60 s).
// 10000UL keeps the Tinkercad run short. Only the SLEEP time changes with CYCLE_MS: the
// battery-life estimates use the measured ACTIVE charge plus the sleep floor over the rest of
// the interval (same method as estimate() in nano_duty_cycle), so they come out the same.
// The "WHOLE CYCLE" average current, however, is for CYCLE_MS, so use 60000UL for Table 2.
const unsigned long CYCLE_MS = 10000UL;   // lab uses 60000UL

const unsigned long MODE_MS = 1000;       // window per Nano mode (single-mode sketches print 1 s windows)
#define REPEAT_RUNS 0                     // 0 = run everything once and stop; 1 = repeat forever

// Nano node profile (from nano_duty_cycle.ino)
const float NANO_SCALE = 1;               // 1 sim mA = 1 real mA
const unsigned long SENSE_MS = 100;       // PLACEHOLDER: time to wake, read T/H. Measure on hardware.
const unsigned long ADV_MS = 2000;        // lab procedure: advertise for 2 s
// nRF52840 System ON idle, 1.5 uA typ. (Nordic nRF52840 Product Specification). Chip only.
const float NANO_SLEEP_FLOOR_mA = 0.0015;

// Battery: 2600 mAh lab pack, and the derated capacity at 5 V (write-up B8, item 7):
// 2600 mAh x 3.7 V / 5 V x 0.85 (assumed boost efficiency) = 1635 mAh
const float BATTERY_mAh = 2600.0;
const float BATTERY_DERATED_mAh = 2600.0 * 3.7 / 5.0 * 0.85;

// =====================================================================
//  Results kept for the summary
// =====================================================================

float modeI[6];          // Nano mode a-f average current (mA)
DutyResult nanoRes;
unsigned long runNo = 0;

// One active state: sample every 5 ms until ms have passed (same as the duty-cycle sketches).
// t0 = when the state started (so work done at the start of a state counts towards its time).
void runStateFrom(void *pm, void *pcyc, unsigned long t0, unsigned long ms) {
  Meter &m = *(Meter *)pm; Meter &cyc = *(Meter *)pcyc;
  while (millis() - t0 < ms) { m.sample(); cyc.sample(); delay(5); }
}

void runState(void *pm, void *pcyc, unsigned long ms) {
  runStateFrom(pm, pcyc, millis(), ms);
}

// Sleep until the scheduled end of the cycle, sampling every 50 ms (same as the duty-cycle sketches).
void runSleep(void *pm, void *pcyc, unsigned long tCycleStart) {
  Meter &m = *(Meter *)pm; Meter &cyc = *(Meter *)pcyc;
  setLoads(0,0,0,0,0);
  m.start();
  while (millis() - tCycleStart < CYCLE_MS) { m.sample(); cyc.sample(); delay(50); }
}

// Copy the meters into a DutyResult (scaled to real mA / mC).
void finishDuty(void *pr, void *pState, uint8_t n, void *pSleep, void *pCycle,
                float scale, float floor_mA) {
  DutyResult &r = *(DutyResult *)pr; Meter *mState = (Meter *)pState;
  Meter &mSleep = *(Meter *)pSleep; Meter &mCycle = *(Meter *)pCycle;
  r.nStates = n;
  r.qActive_mC = 0; r.tActive_ms = 0;
  for (uint8_t i = 0; i < n; i++) {
    r.tState_ms[i] = mState[i].elapsed_ms();
    r.iState_mA[i] = mState[i].avg_mA() * scale;
    r.qState_mC[i] = mState[i].charge_mC() * scale;
    r.qActive_mC += r.qState_mC[i];
    r.tActive_ms += r.tState_ms[i];
  }
  r.tSleep_ms = mSleep.elapsed_ms();
  r.iSleepMeas_mA = mSleep.avg_mA() * scale;
  r.iSleep_mA = r.iSleepMeas_mA > floor_mA ? r.iSleepMeas_mA : floor_mA;
  r.tCycle_ms = mCycle.elapsed_ms();
  r.iCycle_mA = mCycle.avg_mA() * scale;
  r.qCycle_mC = mCycle.charge_mC() * scale;
}

void printActiveLine(void *pr, float floor_mA) {
  DutyResult &r = *(DutyResult *)pr;
  Serial.print(F("Q_active_mC="));      Serial.print(r.qActive_mC, 3);
  Serial.print(F(", t_active_ms="));    Serial.print(r.tActive_ms, 0);
  Serial.print(F(", I_sleep_used_mA=")); Serial.print(r.iSleep_mA, 4);
  Serial.println(r.iSleepMeas_mA > floor_mA ? F(" (measured)") : F(" (datasheet floor)"));
}

// Average current at a reporting interval (same formula as estimate() in the duty-cycle sketches):
// I_avg = (Q_active + I_sleep x (T - t_active)) / T
float avgAtInterval(void *pr, float interval_s) {
  DutyResult &r = *(DutyResult *)pr;
  float tSleep_s = interval_s - r.tActive_ms / 1000.0;
  if (tSleep_s < 0) tSleep_s = 0;
  return (r.qActive_mC + r.iSleep_mA * tSleep_s) / interval_s;   // mC/s = mA
}

// =====================================================================
//  Part 1: Nano modes a-f
// =====================================================================
// Pins on per mode, as in nano_mode_a..f and nano_mode_sweep: MCU, SENS_PWR, SENS_CONV, MIC, RADIO
const uint8_t MODE_LOADS[6][5] = {
  {1,1,0,0,0},   // a idle in loop(), sensors begun
  {1,1,1,0,0},   // b reading all sensors (T and RH every 100 ms)
  {1,0,0,1,0},   // c PDM audio
  {1,0,0,0,1},   // d BLE advertising (radio on)
  {1,0,0,0,0},   // e idle, sensors powered down (end())
  {0,0,0,0,0} }; // f sleep, everything off

void runModes() {
  Serial.println(F("--- Part 1: Nano node modes a-f (1 s each) ---"));
  for (uint8_t k = 0; k < 6; k++) {
    setLoads(MODE_LOADS[k][0], MODE_LOADS[k][1], MODE_LOADS[k][2], MODE_LOADS[k][3], MODE_LOADS[k][4]);
    delay(50);                       // let the circuit settle (as in nano_mode_sweep)
    Meter m; m.start();
    unsigned long t0 = millis(), lastRead = 0;
    while (millis() - t0 < MODE_MS) {
      if (k == 1 && millis() - lastRead >= 100) { lastRead = millis(); readTempC(); readHumidity(); }
      m.sample();
      delay(5);
    }
    Serial.print(F("MODE ")); Serial.print((char)('a' + k));
    m.print(F(""), NANO_SCALE);
    if (k == 1) {                    // sensors are still powered here (D7 on)
      Serial.print(F("  T_C=")); Serial.print(readTempC(), 1);
      Serial.print(F(", RH_pct=")); Serial.println(readHumidity(), 1);
    }
    modeI[k] = m.avg_mA() * NANO_SCALE;
  }
  setLoads(0,0,0,0,0);
}

// =====================================================================
//  Part 2: Nano duty cycle (one cycle)
// =====================================================================
void runNanoDuty() {
  Serial.println(F("--- Part 2: Nano duty cycle ---"));
  Meter mState[2], mSleep, mCycle;
  unsigned long tCycle = millis();
  mCycle.start();

  // ---- state 1: WAKE+SENSE (sensors powered, read T and RH) ----
  setLoads(1,1,1,0,0);
  mState[0].start();
  unsigned long tSense = millis();             // as in nano_duty_cycle: timed from before the reads
  nanoRes.tempC = readTempC(); nanoRes.rh = readHumidity();
  runStateFrom(&mState[0], &mCycle, tSense, SENSE_MS);

  // ---- state 2: ADVERTISE ----
  setLoads(1,0,0,0,1);
  mState[1].start();
  runState(&mState[1], &mCycle, ADV_MS);

  // ---- SLEEP until the next wake-up ----
  runSleep(&mSleep, &mCycle, tCycle);

  Serial.print(F("=== Nano cycle  T_C=")); Serial.print(nanoRes.tempC, 1);
  Serial.print(F("  RH_pct=")); Serial.println(nanoRes.rh, 1);
  mState[0].print(F("WAKE+SENSE"), NANO_SCALE);
  mState[1].print(F("ADVERTISE"), NANO_SCALE);
  mSleep.print(F("SLEEP"), NANO_SCALE);
  mCycle.print(F("WHOLE CYCLE"), NANO_SCALE);
  finishDuty(&nanoRes, mState, 2, &mSleep, &mCycle, NANO_SCALE, NANO_SLEEP_FLOOR_mA);
  printActiveLine(&nanoRes, NANO_SLEEP_FLOOR_mA);
}

// =====================================================================
//  Part 3: summary and battery-life estimates
// =====================================================================
void printPct(float part, float whole) {
  Serial.print(whole > 0 ? 100.0 * part / whole : 0, 1); Serial.print('%');
}

// Per-state charge shares at 60 / 600 / 3600 s, then I_avg and life at both capacities.
void printBoard(const __FlashStringHelper *board, void *pr, const __FlashStringHelper *n0,
                const __FlashStringHelper *n1) {
  DutyResult &r = *(DutyResult *)pr;
  const float intervals[3] = {60, 600, 3600};
  Serial.print(F("[")); Serial.print(board);
  Serial.print(F("] Q_active_mC=")); Serial.print(r.qActive_mC, 3);
  Serial.print(F(", t_active_ms=")); Serial.print(r.tActive_ms, 0);
  Serial.print(F(", I_sleep_used_mA=")); Serial.println(r.iSleep_mA, 4);
  for (uint8_t k = 0; k < 3; k++) {
    float T = intervals[k];
    float iAvg = avgAtInterval(&r, T);
    float qTotal = iAvg * T;                                 // mC per cycle at this interval
    float tSleep_s = T - r.tActive_ms / 1000.0; if (tSleep_s < 0) tSleep_s = 0;
    float qSleep = r.iSleep_mA * tSleep_s;
    Serial.print(F("  interval_s=")); Serial.print(T, 0);
    Serial.print(F(", I_avg_mA="));   Serial.print(iAvg, 4);
    Serial.print(F(", life_h="));     Serial.print(BATTERY_mAh / iAvg, 1);
    Serial.print(F(", life_days="));  Serial.print(BATTERY_mAh / iAvg / 24.0, 1);
    Serial.print(F(", derated_days=")); Serial.println(BATTERY_DERATED_mAh / iAvg / 24.0, 1);
    Serial.print(F("    shares: ")); Serial.print(n0); Serial.print(' '); printPct(r.qState_mC[0], qTotal);
    Serial.print(F(", ")); Serial.print(n1); Serial.print(' '); printPct(r.qState_mC[1], qTotal);
    Serial.print(F(", SLEEP ")); printPct(qSleep, qTotal); Serial.println();
  }
}

void printSummary() {
  Serial.println(F("--- Part 3: summary ---"));
  Serial.println(F("Nano modes, I_avg_mA:"));
  for (uint8_t k = 0; k < 6; k++) {
    Serial.print(F("  ")); Serial.print((char)('a' + k)); Serial.print(F(": "));
    Serial.println(modeI[k], 3);
  }
  Serial.print(F("Cycle length used: CYCLE_MS=")); Serial.print(CYCLE_MS);
  Serial.println(F(" (lab: 60000). Estimates below do not depend on it."));
  Serial.print(F("Battery: ")); Serial.print(BATTERY_mAh, 0);
  Serial.print(F(" mAh nominal; derated = 2600 x 3.7/5 x 0.85 = ")); Serial.print(BATTERY_DERATED_mAh, 0);
  Serial.println(F(" mAh at 5 V"));
  printBoard(F("Nano"), &nanoRes, F("WAKE+SENSE"), F("ADVERTISE"));
  Serial.print(F("T_C / RH_pct read in the duty cycle: ")); Serial.print(nanoRes.tempC, 1);
  Serial.print(F(" / ")); Serial.println(nanoRes.rh, 1);
}

// =====================================================================
void setup() {
  Serial.begin(9600);
  setupPins();
  Serial.println(F("lab3b_unified started"));
}

void loop() {
  runNo++;
  Serial.print(F("##### Run ")); Serial.println(runNo);
  runModes();
  runNanoDuty();
  printSummary();
  setLoads(0,0,0,0,0);
#if REPEAT_RUNS
  Serial.println(F("Repeating..."));
#else
  Serial.println(F("Done. Set REPEAT_RUNS 1 to repeat. Stopped."));
  while (true) { delay(1000); }      // stop here, all loads off
#endif
}
