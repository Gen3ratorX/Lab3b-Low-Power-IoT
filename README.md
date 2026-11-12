# Lab 3b: Low-Power Design for IoT Nodes (Tinkercad emulation)

I am Dominic Aidoo (MSc Advanced IoT Systems and Design). This repository is my Lab 3b work: I emulate the current profile of a Nano 33 BLE Sense node and an ESP32 gateway in Tinkercad Circuits, integrate the charge over a duty cycle, and estimate battery life.

## Tinkercad circuit

**[Open the shared circuit](https://www.tinkercad.com/things/e1XMXFFtFPU?sharecode=Imbmm84jeN2KRPMMlXtBHV5mv_u6Wey7HfUIzokHml4)**

Editor link: <https://www.tinkercad.com/things/e1XMXFFtFPU/editel?returnTo=%2Fdashboard&sharecode=Imbmm84jeN2KRPMMlXtBHV5mv_u6Wey7HfUIzokHml4>

Tinkercad has no Nano 33 BLE Sense and no ESP32, so the circuit is an Arduino Uno R3. I switch resistor loads on D3–D8 to stand in for the node: D3 sensor conversion (3.3 kΩ), D4 MCU active (1.5 kΩ), D5 radio TX (750 Ω), D6 PDM microphone (4.7 kΩ), and D8 an optional board-overhead LED (4.7 kΩ). The TMP36 and the 10 kΩ “humidity” potentiometer are powered from D7, so they draw current only while the sensors are on. Every load returns through a separate node-ground rail. A 22 Ω shunt sits between that rail and Uno GND, and A0 reads the shunt with the internal 1.1 V reference (about 0.05 mA per ADC step). The same loads stand in for the ESP32, scaled 1:20, so 1 simulated mA represents 20 mA.

## Sketches

Each folder is named after its `.ino`, so the Arduino IDE opens it as a sketch. I did not change the sketch sources.

| Sketch | From the file header |
|---|---|
| `sketches/lab3b_unified` | One sketch for the whole Tinkercad lab: Nano modes a–f, the Nano duty cycle, the ESP32 duty cycle scaled ×20, then charge shares and battery-life estimates at 1, 10 and 60 min (2600 mAh and the derated 1635 mAh). |
| `sketches/nano_mode_a_idle_loop` | Mode a: idle in `loop()`, MCU running, sensors begun but not converting. |
| `sketches/nano_mode_b_read_sensors` | Mode b: reading all sensors continuously. |
| `sketches/nano_mode_c_pdm_audio` | Mode c: recording audio with the PDM microphone. |
| `sketches/nano_mode_d_ble_advertising` | Mode d: advertising over BLE, with the radio load on continuously (an upper bound). |
| `sketches/nano_mode_e_sensors_powered_down` | Mode e: idle in `loop()` with the sensors powered down, as after the libraries’ `end()` calls. |
| `sketches/nano_mode_f_sleep` | Mode f: MCU asleep and everything off, the idle part of the duty cycle. |
| `sketches/nano_mode_sweep` | Runs Nano modes a–f for 5 s each and prints one summary line per mode. |
| `sketches/nano_duty_cycle` | One cycle is WAKE+SENSE, then ADVERTISE, then SLEEP; it prints charge per cycle and battery life at 1, 10 and 60 min. |
| `sketches/esp32_mode_boot_cpu_active` | ESP32 mode B, scaled 1:20: boot / CPU active, radio off (modem-sleep-like). |
| `sketches/esp32_mode_wifi_connect` | ESP32 mode C, scaled 1:20: Wi-Fi connect / receive and listen. |
| `sketches/esp32_mode_wifi_transmit` | ESP32 mode T, scaled 1:20: Wi-Fi transmit. |
| `sketches/esp32_mode_deep_sleep` | ESP32 mode S, scaled 1:20: deep sleep (`esp_deep_sleep_start` with a timer wake-up). |
| `sketches/esp32_duty_cycle` | ESP32 cycle BOOT → WIFI_CONNECT → TRANSMIT → SLEEP, scaled 1 simulated mA = 20 real mA; prints charge per cycle and battery life at 1, 10 and 60 min. Connect and transmit times are placeholders. |
| `sketches/hardware_reference/nano33_duty_cycle_hw` | Real Nano 33 BLE Sense Rev2 sketch (not for Tinkercad): wake every 60 s, read temperature and humidity, advertise for 2 s, then idle, with an INA219 in series on VIN. |
| `sketches/hardware_reference/esp32_duty_cycle_hw` | Real ESP32 / Wokwi sketch (not for Tinkercad): Wi-Fi connect, transmit, then deep sleep with a timer wake-up. |

## How to run

1. Open the shared circuit above.
2. Open **Code → Text**, delete the default sketch, and paste the whole of `sketches/lab3b_unified/lab3b_unified.ino`.
3. Start the simulation.
4. Open the Serial Monitor and read the mode currents, the charge per cycle, and the battery-life lines.

The other Tinkercad sketches paste in the same way. The two files under `hardware_reference/` are for the real Nano 33 BLE Sense and ESP32 kit, not for this circuit.

## Key results

The lab write-up has no simulator readings. The figures below are only the numbers it already gives: calculated load targets, and one worked example built from those targets and placeholder timings. They are not measurements from my run.

**Calculated Nano mode currents** (Table 1 targets): idle with sensors begun ≈ 3.4 mA, reading sensors ≈ 4.8 mA, PDM audio ≈ 4.3 mA, BLE advertising ≈ 9.6 mA, idle with sensors powered down ≈ 3.3 mA, sleep ≈ 0. While a duty cycle runs, the multimeter is expected to step through about 4.8 mA (sense), about 9.6 mA (advertise) and 0 (sleep).

**Calculated ESP32 states, scaled ×20:** boot / CPU active ≈ 3.3 mA simulated ≈ 66 mA, Wi-Fi connect / RX ≈ 4.75 mA ≈ 95 mA, Wi-Fi transmit ≈ 11.9 mA ≈ 238 mA, deep sleep ≈ 0 on the shunt (the sketch uses the 10 µA datasheet floor).

**Worked example, not a measurement.** Nano: sense is 0.1 s at 4.84 mA (0.484 mC) and advertise is 2.0 s at 9.58 mA (19.16 mC), so the active charge is 19.64 mC over 2.1 s. With a 1.5 µA sleep floor, average current is 0.329 mA at 60 s (about 7 900 h, about 329 days on 2600 mAh, about 207 days derated), about 0.0342 mA at 600 s (about 3 160 days, about 1 990 days derated), and about 0.0070 mA at 3600 s, where sleep is about 22 % of the charge. ESP32: boot 0.3 s at 66 mA, connect 2.0 s at 95 mA and transmit 0.2 s at 238 mA give about 257 mC over 2.5 s. Average current is about 4.30 mA at 60 s (about 605 h, about 25 days on 2600 mAh, about 16 days derated), about 0.439 mA at 600 s (about 247 days), and about 0.082 mA at 3600 s (about 1 330 days). Derating uses 2600 mAh × 3.7 V / 5 V × 0.85 ≈ 1635 mAh.
