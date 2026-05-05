/*
 * ============================================================
 *  Automatic Temperature & Humidity Controller
 *  Fan Speed Control + Mist Regulation
 * ============================================================
 *  Hardware:
 *    - Arduino Uno (ATmega328P)
 *    - LM35  → LM358 IC1 (non-inverting amp, gain 11×) → A0
 *    - DHT11 → D7  (10kΩ pull-up to 5V)
 *    - LM358 IC2 comparator → D8  (hardware interlock)
 *    - L293D H-bridge → DC fan motor  (Enable1 on D9 PWM)
 *    - Mist module on D6 (manual in this build)
 *    - 16×2 I²C LCD on A4 (SDA) / A5 (SCL)
 *    - LEDs: Blue D2 | Green D3 | Yellow D4 | Red D5
 *
 *  Libraries required (install via Arduino IDE → Manage Libraries):
 *    - DHT sensor library  by Adafruit
 *    - LiquidCrystal I2C   by Frank de Brabander
 *
 *  Authors: Jay (25110147) · Shubham (25110278) · Harshal (25110127)
 *  IIT Gandhinagar — ES116 Principles of Electrical Engineering
 * ============================================================
 */

#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// ── Pin Definitions ───────────────────────────────────────────
#define LM35_PIN      A0   // LM358 IC1 amplified output → ADC
#define DHT_PIN       7    // DHT11 data (10kΩ pull-up to 5V)
#define HW_INTERLOCK  8    // LM358 IC2 comparator output → hardware safety
#define FAN_PWM       9    // L293D Enable1 — PWM speed control (31.25 kHz)
#define MIST_PWM      6    // Mist module control (on/off)
#define LED_BLUE      2    // Zone 1 indicator: 20–25°C
#define LED_GREEN     3    // Zone 2 indicator: 25–30°C
#define LED_YELLOW    4    // Zone 3 indicator: 30–35°C
#define LED_RED       5    // Zone 4 indicator: >35°C

// ── Objects ───────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT11);            // DHT11 (not DHT22)
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change to 0x3F if LCD stays blank

/*
 * ── Gain = 11× ───────────────────────────────────────────────
 * LM35 output:      10 mV/°C
 * After LM358 amp:  110 mV/°C  (Rf = 10kΩ, R1 = 1kΩ → gain = 1 + 10/1 = 11)
 * At 25°C:          2.75V → ADC ~564 counts
 *
 * Temperature formula:
 *   Vout = ADC_reading × (5.0 / 1023.0)
 *   Temp  = Vout / (0.01 × 11.0)
 */

// ── Median Filter (7 samples) ─────────────────────────────────
// Removes ADC noise spikes from LM35 by sorting 7 readings
// and returning the middle value.
int medianRead(int pin) {
  int b[7];
  for (int i = 0; i < 7; i++) {
    b[i] = analogRead(pin);
    delay(4);
  }
  // Insertion sort
  for (int i = 1; i < 7; i++) {
    int k = b[i], j = i - 1;
    while (j >= 0 && b[j] > k) {
      b[j + 1] = b[j];
      j--;
    }
    b[j + 1] = k;
  }
  return b[3]; // Median (4th of 7 sorted values)
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  // Set Timer1 prescaler to 1 → D9 PWM frequency = 31.25 kHz
  // This pushes PWM above audible range, reducing fan whine.
  TCCR1B = (TCCR1B & 0xF8) | 0x01;

  // Pin modes
  pinMode(FAN_PWM,      OUTPUT);
  pinMode(MIST_PWM,     OUTPUT);
  pinMode(HW_INTERLOCK, INPUT);   // D8 reads LM358 IC2 comparator
  pinMode(LED_BLUE,     OUTPUT);
  pinMode(LED_GREEN,    OUTPUT);
  pinMode(LED_YELLOW,   OUTPUT);
  pinMode(LED_RED,      OUTPUT);
  pinMode(12,           OUTPUT);  // L293D Input1 — direction pin
  digitalWrite(12, HIGH);         // Fixed HIGH = forward rotation

  // Initialise sensors and display
  dht.begin();
  lcd.init();
  lcd.backlight();

  // Startup splash screen
  lcd.setCursor(0, 0);
  lcd.print("  Fan + Mist   ");
  lcd.setCursor(0, 1);
  lcd.print(" Initialising..");
  delay(2000);
  lcd.clear();
}

// ── Main Loop ─────────────────────────────────────────────────
void loop() {

  // ── 1. Read Temperature from LM35 via LM358 amp ─────────────
  float Vin  = medianRead(LM35_PIN) * (5.0 / 1023.0); // ADC → volts
  float temp = Vin / (0.01 * 11.0);                   // Volts → °C (gain 11×)

  // ── 2. Read Humidity from DHT11 ─────────────────────────────
  float rh = dht.readHumidity();
  if (isnan(rh)) rh = 50.0; // Fallback if DHT11 read fails

  // ── 3. Fan PWM — Temperature Zones ──────────────────────────
  //
  //  Zone     Temp range    PWM   Fan speed
  //  ──────   ──────────   ─────  ─────────
  //  Standby  < 20°C         0     Off
  //  Zone 1   20–25°C       76     30%
  //  Zone 2   25–30°C      153     60%
  //  Zone 3   30–35°C      204     80%
  //  Zone 4   > 35°C       255    100%
  //
  int fanPWM = 0;
  if      (temp < 20) fanPWM = 0;
  else if (temp < 25) fanPWM = 76;   // 30%
  else if (temp < 30) fanPWM = 153;  // 60%
  else if (temp < 35) fanPWM = 204;  // 80%
  else                fanPWM = 255;  // 100%

  // Humidity boost: humid air feels hotter — increase fan by 25 counts
  if (rh > 65 && fanPWM > 0)
    fanPWM = min(255, fanPWM + 25);

  // ── 4. Mist Control — Humidity Threshold ────────────────────
  // Mist ON when dry (RH < 60%), OFF when humid.
  // In current build, mist module is manually operated;
  // D6 signal is still set correctly for future automated use.
  int mistPWM = 0;
  if (rh < 60) mistPWM = 255; // Dry or moderate → mist ON
  else         mistPWM = 0;   // Humid → mist OFF

  // ── 5. Hardware Interlock — LM358 IC2 Comparator ────────────
  // LM358 IC2 monitors humidity independently of software.
  // When RH ≥ ~60%, comparator pulls D8 LOW.
  // This overrides software and cuts mist output immediately,
  // regardless of what the main loop has calculated.
  if (!digitalRead(HW_INTERLOCK)) mistPWM = 0; // Hardware wins

  // ── 6. Write Outputs ────────────────────────────────────────
  analogWrite(FAN_PWM,  fanPWM);
  analogWrite(MIST_PWM, mistPWM);

  // ── 7. Zone LEDs ────────────────────────────────────────────
  digitalWrite(LED_BLUE,   (temp >= 20 && temp < 25));
  digitalWrite(LED_GREEN,  (temp >= 25 && temp < 30));
  digitalWrite(LED_YELLOW, (temp >= 30 && temp < 35));
  digitalWrite(LED_RED,    (temp >= 35));

  // ── 8. LCD Display ──────────────────────────────────────────
  // Line 1: Temperature reading + Fan percentage
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temp, 1);          // 1 decimal place
  lcd.print((char)223);        // Degree symbol °
  lcd.print("C F:");
  lcd.print((int)(fanPWM / 2.55));
  lcd.print("%  ");            // Trailing spaces clear old chars

  // Line 2: Humidity reading + Mist status
  lcd.setCursor(0, 1);
  lcd.print("H:");
  lcd.print(rh, 0);            // 0 decimal places
  lcd.print("% M:");
  lcd.print(mistPWM > 0 ? "ON " : "OFF");

  delay(500); // Poll every 500 ms
}
