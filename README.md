# Automatic Temperature & Humidity Controller
### Fan Speed Control + Mist Regulation — Arduino Uno

**Jay Glasswala** · **Shubham Ruparel** · **Harshal Thanki**  
ES 116 Project, IIT Gandhinagar

---

## Overview

An embedded system that continuously monitors room temperature and humidity and responds with two actuators:

- **DC Fan** — speed controlled via PWM across four temperature zones
- **Ultrasonic Mist Module** — switched on/off based on humidity threshold

An LM35 temperature sensor is amplified 11× by an LM358 non inverting op-amp before being read by the Arduino ADC, giving reliable resolution across the 20–40°C operating range. A DHT11 sensor provides humidity data. A second LM358 configured as a comparator implements a **hardware interlock** that disables misting when humidity is too high, independently of software (security layer over software).

Real-time feedback is provided via a 16×2 I2C LCD and four colour-coded zone LEDs.

---

## System Block Diagram

```
5V Supply Rail ─────────────────────────────────────────────────────────────
                                                                            
  LM35          LM358 IC1          Arduino Uno           L293D        DC Fan
  (Temp)  ───►  (Amp, 11×)  ───►  A0                    H-Bridge  ►  6" Blade
  10mV/°C       110mV/°C           D9 ──PWM──────────►  Enable1
                                   D12 ─────────────►   Input1 (dir)

  DHT11         ─────────────────► D7  (digital data)
  (Humidity)                       D8 ◄── LM358 IC2 ──► (HW interlock)
  10kΩ pull-up                     D6 ──────────────────────────────►  Mist
                                   A4/A5 ──────────────────────────►  LCD I2C
  LM358 IC2                        D2–D5 ──────────────────────────►  Zone LEDs
  (Comparator)
  2×10kΩ divider → 2.5V ref
```

---

## Hardware

### Components

| Component | Specification | Role |
|---|---|---|
| Arduino Uno | ATmega328P | Central controller |
| LM35 | TO-92, 10 mV/°C | Temperature sensor |
| DHT11 | ±5% RH | Humidity sensor |
| LM358 IC1 | DIP-8, dual op-amp | LM35 signal amplifier (gain 11×) |
| LM358 IC2 | DIP-8, dual op-amp | Hardware interlock comparator |
| L293D | DIP-16, H-bridge 600mA | Fan PWM motor driver |
| DC Motor | INVENTO 6V–12V, 2.3mm shaft | Fan drive |
| Fan Blade | 6-inch 3-blade, 2.3mm fit | Airflow |
| Mist Module | 108kHz ultrasonic, 5V 400mA | Humidity actuation |
| 16×2 LCD | I2C backpack, address 0x27 | Display |
| LEDs | Blue / Green / Yellow / Red | Zone indicator |
| Resistors | 1kΩ, 10kΩ (×5), 220Ω (×4), 4.7kΩ | Various |
| Capacitors | 10µF, 100µF | Decoupling |

### Wiring Summary

**LM35 → LM358 IC1 Amplifier (Gain = 11×)**
- LM35 OUT → IC1 Pin 3 (IN+)
- IC1 Pin 2 (IN−) → junction of 10kΩ feedback + 1kΩ to GND
- IC1 Pin 1 (OUT) → 10kΩ → IC1 Pin 2 (feedback loop)
- IC1 Pin 1 (OUT) → Arduino A0

**DHT11**
- DATA → Arduino D7
- 10kΩ pull-up between DATA and 5V *(mandatory)*

**LM358 IC2 Comparator (Hardware Interlock)**
- IN− (Pin 2) → 2×10kΩ voltage divider → 2.5V reference
- IN+ (Pin 3) → 10kΩ → DHT11 DATA line
- OUT (Pin 1) → 10kΩ → Arduino D8 + 10kΩ pull-down to GND

**L293D Fan Driver**
- Pin E1 (Enable1) → Arduino D9 (PWM)
- Pin I1 (Input1) → Arduino D12 (HIGH = forward)
- Pin I2 (Input2) → GND
- Pin O1 + Pin O2 → Fan motor terminals
- Pins -ve, GND → GND | Pin +ve → 5V | Pin VS → 5V

**LEDs** (each with 220Ω series resistor)
- D2 → Blue | D3 → Green | D4 → Yellow | D5 → Red

**LCD** — SDA → A4 | SCL → A5

---

## Operating Zones

| Temp (°C) | RH (%) | Fan PWM | Fan % | Mist | LED |
|---|---|---|---|---|---|
| < 20 | Any | 0 | 0% | Off | None |
| 20–25 | < 60 | 76 | 30% | On | 🔵 Blue |
| 25–30 | < 60 | 153 | 60% | On | 🟢 Green |
| 30–35 | < 60 | 204 | 80% | On | 🟡 Yellow |
| > 35 | < 60 | 255 | 100% | On | 🔴 Red |
| Any | ≥ 60 | +25 boost | — | Off (HW interlock) | Active zone |

**Humidity boost:** When RH > 65%, fan PWM is increased by 25 counts to compensate for humid air feeling hotter.  
**Hardware interlock:** When RH ≥ 60%, LM358 IC2 pulls D8 LOW → firmware immediately cuts mist output, independent of the main software loop.

---

## Software

### Dependencies

Install both libraries via **Arduino IDE → Tools → Manage Libraries**:

- `DHT sensor library` by Adafruit
- `LiquidCrystal I2C` by Frank de Brabander

### LCD I2C Address

The default address in code is `0x27`. If your LCD shows nothing after upload, change the constructor in `main.ino`:

```cpp
LiquidCrystal_I2C lcd(0x27, 16, 2);  // try 0x3F if blank
```

### Key Implementation Notes

- **Timer1 prescaler** is set to 1 in `setup()` via `TCCR1B`, pushing D9 PWM frequency to 31.25 kHz — above audible range, reducing motor whine.
- **Median filter** (7 samples, insertion sort) on A0 suppresses ADC noise from LM35.
- **Temperature formula:** `T = (ADC × 5.0/1023) / (0.01 × 11)` — accounts for 11× LM358 gain.
- **D12 fixed HIGH** in `setup()` permanently sets L293D direction to forward.

---

## Design Iterations

### A. LC Filter Removed
Original design placed an 18 mH / 100 µF LC low-pass filter between 5V rail and L293D VS pin to smooth PWM into clean DC. During testing, the inductor's DC series resistance caused voltage drop sufficient to stall the motor at Zone 1 (30% PWM). Filter was removed; VS connected directly to 5V.

### B. MOSFET Removed
Original design used an IRFZ44N MOSFET on D6 to switch mist module power. Without soldering equipment, wiring to the mist module's PCB pads was not feasible. MOSFET removed. Mist module powered independently via USB; onboard button toggled manually in response to humidity readings on LCD. Hardware interlock comparator on D8 remains fully functional.

### C. DHT22 → DHT11
Original BOM specified DHT22 (±0.5°C, ±2% RH). Available component was DHT11 (±2°C, ±5% RH). Since temperature control uses only LM35, DHT11 inaccuracy does not affect fan zone decisions. Humidity thresholds widened in firmware to account for ±5% accuracy.

---

## Troubleshooting

| Problem | Likely Cause | Fix |
|---|---|---|
| LCD blank, backlight on | Wrong I2C address | Change `0x27` → `0x3F` in code |
| LCD blank, backlight off | Power/wiring issue | Check VCC and GND on LCD |
| Temperature reads 0 or garbage | LM358 IC1 feedback wrong | Check: 1kΩ from Pin 2 to GND, 10kΩ from Pin 1 to Pin 2 |
| Temperature reads very high | Wrong gain resistors | Verify Rf = 10kΩ, R1 = 1kΩ |
| Fan doesn't spin at Zone 1 | L293D 1.4V dropout at 5V | Increase Zone 1 `fanPWM` from 76 to 110 in code (which is on scale of 0 to 255)|
| Fan spins backwards | Motor wires swapped | Swap O1 and O2 connections on L293D |
| DHT11 always returns NaN | Missing pull-up | Add 10kΩ between D7 and 5V |
| Upload fails (avrdude error) | Wire on D0 or D1 | Remove all wires from D0/D1 before uploading |

---

## Repository Structure

```
.
├── ES116_Demo_of_project.jpeg    ← Photo of full project in single frame
├── README.md                     ← This file
├── main.ino                      ← Arduino sketch (upload via Arduino IDE)

```

---

## References

1. Texas Instruments, *LM35 Precision Centigrade Temperature Sensors*, Datasheet SNIS159H, 2017.  
2. STMicroelectronics, *L293D — Push-pull four-channel driver with diodes*, Datasheet, 2023.  
3. Texas Instruments, *LM358 — Dual Operational Amplifiers*, Datasheet SNOSBT5H, 2015.  
4. Aosong Electronics, *DHT11 — Temperature and Humidity Sensor*, Product Manual V1.3, 2010.  
5. Atmel Corporation, *ATmega328P — 8-bit AVR Microcontroller*, Datasheet, 2015.
