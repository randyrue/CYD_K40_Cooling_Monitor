// =============================================================================
//  K40 Laser Cooling Monitor — ESP32 Cheap Yellow Display (ESP32-2432S028)
// =============================================================================
//
//  Hardware connections:
//    DS18B20 data        -> GPIO 22  (P3 connector), 4.7kOhm pullup to 3.3V
//    DS18B20 VCC         -> 5V
//    DS18B20 GND         -> GND
//    Flow sensor signal  -> GPIO 35  (P3 connector)
//    Flow sensor VCC     -> 5V
//    Flow sensor GND     -> GND
//    Laser relay signal  -> GPIO 27  (CN1 connector)
//    Speaker (onboard)   -> GPIO 26  (SPEAK connector)
//    Power/GND           -> Serial port next to the USB C port (P1 connector)  
//
//  Relay wiring:
//    GPIO 27 is 3.3V logic. Use a relay module with 3.3V trigger, or drive
//    a 5V relay coil via a small NPN transistor (e.g. 2N2222). Relay output goes
//    connects between the existing connection to the "P" pin on the high voltage
//    power supply.
//
//  DS18B20 pullup:
//    4.7kOhm resistor between GPIO 22 and 5V. Note that specs call to connect it
//    to 3.3V but 5V seems to work and simplifies wiring.
//
//  Libraries required (Arduino Library Manager):
//    TFT_eSPI by Bodmer
//    XPT2046_Touchscreen by Paul Stoffregen
//    OneWire by Paul Stoffregen
//    DallasTemperature by Miles Burton
//
//  See User_Setup.h block at the bottom of this file. 
//
//  Boots in live mode. Hold the bottom button 3 seconds to toggle test mode.
//  Set minFlow to 0.00 L/m to disable flow fault checking entirely.
// =============================================================================

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------------------------------------------------------------------
//  Pin definitions
// ---------------------------------------------------------------------------
#define FLOW_SENSOR_PIN     35
#define DS18B20_PIN         22
#define RELAY_PIN           27
#define SPEAKER_PIN         26
#define TOUCH_CS            33
#define TOUCH_IRQ           36

// ---------------------------------------------------------------------------
//  DS18B20
// ---------------------------------------------------------------------------
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);

// ---------------------------------------------------------------------------
//  Flow sensor
// ---------------------------------------------------------------------------
#define FLOW_CALIBRATION    98.0f      // pulses per litre/minute

// ---------------------------------------------------------------------------
//  Defaults
// ---------------------------------------------------------------------------
#define DEFAULT_MAX_TEMP    24.0f      // C   — above this triggers alarm
#define DEFAULT_MIN_FLOW     0.8f      // L/min — below this triggers alarm (0 = disabled)

// ---------------------------------------------------------------------------
//  Display / touch objects
// ---------------------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// Touch calibration — confirmed from all four corner taps:
//   top-left:    raw( 467,  747)   top-right:    raw(3799,  677)
//   bottom-left: raw( 450, 3520)   bottom-right: raw(3769, 3670)
#define TS_MINX   450
#define TS_MAXX  3799
#define TS_MINY   677
#define TS_MAXY  3670

// ---------------------------------------------------------------------------
//  Globals
// ---------------------------------------------------------------------------
volatile uint32_t pulseCount = 0;
float flowRate  = 0.0f;
float waterTemp = 99.0f;   // safe default — triggers alarm until real reading

float maxTemp = DEFAULT_MAX_TEMP;
float minFlow = DEFAULT_MIN_FLOW;

bool alarmActive  = false;
bool alarmSilent  = false;
bool testMode     = false;   // boots in live mode; hold bottom button 3s to toggle

unsigned long lastFlowCalc   = 0;
unsigned long lastAlarmTone  = 0;
unsigned long muteHoldStart  = 0;
bool          muteHolding    = false;
uint8_t       muteReleaseCnt = 0;   // consecutive no-touch reads to confirm lift-off
#define RELEASE_NEEDED  6           // ~6 x 10ms = 60ms silence = genuine release

enum Screen { SCREEN_MAIN, SCREEN_SET_TEMP, SCREEN_SET_FLOW };
Screen currentScreen = SCREEN_MAIN;

// ---------------------------------------------------------------------------
//  ISR — counts flow sensor pulses
// ---------------------------------------------------------------------------
void IRAM_ATTR flowPulse() { pulseCount++; }

// ---------------------------------------------------------------------------
//  Read DS18B20 — returns 99.0 if disconnected so alarm fires safely
// ---------------------------------------------------------------------------
float readTemperature() {
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) return 99.0f;
  return t;
}

// ---------------------------------------------------------------------------
//  Flow rate — call every second, uses elapsed time for accuracy
// ---------------------------------------------------------------------------
void updateFlowRate() {
  unsigned long now     = millis();
  unsigned long elapsed = now - lastFlowCalc;
  if (elapsed < 1000) return;
  noInterrupts();
  uint32_t pulses = pulseCount;
  pulseCount = 0;
  interrupts();
  flowRate     = ((1000.0f / elapsed) * pulses) / FLOW_CALIBRATION;
  lastFlowCalc = now;
}

// ---------------------------------------------------------------------------
//  Relay / speaker
// ---------------------------------------------------------------------------
void setLaser(bool enable)  { digitalWrite(RELAY_PIN, enable ? HIGH : LOW); }
void soundAlarm()           { tone(SPEAKER_PIN, 3000, 120); }
void silenceAlarm()         { noTone(SPEAKER_PIN); }

// ---------------------------------------------------------------------------
//  Colours
//  0xE007 is byte-swapped green for this ST7789 board variant
// ---------------------------------------------------------------------------
#define COLOR_BG      TFT_BLACK
#define COLOR_HEADER  0x1082
#define COLOR_OK      0xE007
#define COLOR_WARN    TFT_RED
#define COLOR_ACCENT  0xFD20
#define COLOR_TEXT    TFT_WHITE
#define COLOR_SUBTEXT 0xAD75
#define COLOR_BTN     0x2945

// ---------------------------------------------------------------------------
//  Draw rounded button
// ---------------------------------------------------------------------------
void drawButton(int x, int y, int w, int h, const char* label,
                uint16_t bg = COLOR_BTN, uint16_t fg = COLOR_TEXT) {
  tft.fillRoundRect(x, y, w, h, 8, bg);
  tft.drawRoundRect(x, y, w, h, 8, COLOR_SUBTEXT);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString(label, x + w / 2, y + h / 2);
}

// ---------------------------------------------------------------------------
//  Update sensor value cards in place — no fillScreen, no flicker
//  Called every second when alarm state has not changed
// ---------------------------------------------------------------------------
void updateValues() {
  // Temp card — red tint if over limit
  uint16_t tCard = (waterTemp > maxTemp) ? 0x6000 : 0x0841;
  tft.fillRoundRect(8, 40, 148, 100, 10, tCard);
  tft.setTextColor(COLOR_SUBTEXT, tCard);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
  tft.drawString("WATER TEMP", 20, 56);
  char buf[16]; dtostrf(waterTemp, 4, 1, buf); strcat(buf, " C");
  tft.setTextColor(COLOR_TEXT, tCard);
  tft.setTextSize(3); tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, 82, 90);
  tft.setTextSize(1); tft.setTextColor(COLOR_SUBTEXT, tCard);
  char limBuf[24]; snprintf(limBuf, sizeof(limBuf), "Limit: %.1f C", maxTemp);
  tft.drawString(limBuf, 82, 120);

  // Flow card — red tint only if minFlow > 0 and flow is below limit.
  // When minFlow == 0 flow checking is disabled so card stays neutral always.
  uint16_t fCard = (minFlow > 0.0f && flowRate < minFlow) ? 0x6000 : 0x0841;
  tft.fillRoundRect(164, 40, 148, 100, 10, fCard);
  tft.setTextColor(COLOR_SUBTEXT, fCard);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
  tft.drawString("FLOW RATE", 176, 56);
  char fBuf[16]; dtostrf(flowRate, 4, 2, fBuf); strcat(fBuf, " L/m");
  tft.setTextColor(COLOR_TEXT, fCard);
  tft.setTextSize(3); tft.setTextDatum(MC_DATUM);
  tft.drawString(fBuf, 238, 90);
  tft.setTextSize(1); tft.setTextColor(COLOR_SUBTEXT, fCard);
  snprintf(limBuf, sizeof(limBuf), "Min: %.2f L/m", minFlow);
  tft.drawString(limBuf, 238, 120);
}

// ---------------------------------------------------------------------------
//  Full screen redraw — called on alarm state change, screen switch, or boot
// ---------------------------------------------------------------------------
void drawMainScreen(bool redBackground) {
  tft.fillScreen(redBackground ? COLOR_WARN : COLOR_BG);
  tft.fillRect(0, 0, 320, 32, redBackground ? 0x8000 : COLOR_HEADER);
  tft.setTextColor(COLOR_TEXT, redBackground ? 0x8000 : COLOR_HEADER);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(2);
  tft.drawString("K40 Cooling Monitor", 8, 16);

  // Badge always reflects actual alarm state, not background colour
  if (alarmActive) {
    tft.fillRoundRect(240, 6, 72, 22, 6, TFT_YELLOW);
    tft.setTextColor(TFT_RED, TFT_YELLOW);
    tft.setTextDatum(MC_DATUM); tft.setTextSize(2);
    tft.drawString("FAULT", 276, 17);
  } else {
    tft.fillRoundRect(256, 6, 56, 22, 6, COLOR_OK);
    tft.setTextColor(TFT_BLACK, COLOR_OK);
    tft.setTextDatum(MC_DATUM); tft.setTextSize(2);
    tft.drawString("OK", 284, 17);
  }

  // Temp card — red-tinted if over limit
  uint16_t tCard = (waterTemp > maxTemp) ? 0x6000 : 0x0841;
  tft.fillRoundRect(8, 40, 148, 100, 10, tCard);
  tft.setTextColor(COLOR_SUBTEXT, tCard);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
  tft.drawString("WATER TEMP", 20, 56);
  char buf[16]; dtostrf(waterTemp, 4, 1, buf); strcat(buf, " C");
  tft.setTextColor(COLOR_TEXT, tCard);
  tft.setTextSize(3); tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, 82, 90);
  tft.setTextSize(1); tft.setTextColor(COLOR_SUBTEXT, tCard);
  char limBuf[24]; snprintf(limBuf, sizeof(limBuf), "Limit: %.1f C", maxTemp);
  tft.drawString(limBuf, 82, 120);

  // Flow card — red tint only if minFlow > 0 and flow is below limit
  uint16_t fCard = (minFlow > 0.0f && flowRate < minFlow) ? 0x6000 : 0x0841;
  tft.fillRoundRect(164, 40, 148, 100, 10, fCard);
  tft.setTextColor(COLOR_SUBTEXT, fCard);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
  tft.drawString("FLOW RATE", 176, 56);
  char fBuf[16]; dtostrf(flowRate, 4, 2, fBuf); strcat(fBuf, " L/m");
  tft.setTextColor(COLOR_TEXT, fCard);
  tft.setTextSize(3); tft.setTextDatum(MC_DATUM);
  tft.drawString(fBuf, 238, 90);
  tft.setTextSize(1); tft.setTextColor(COLOR_SUBTEXT, fCard);
  snprintf(limBuf, sizeof(limBuf), "Min: %.2f L/m", minFlow);
  tft.drawString(limBuf, 238, 120);

  drawButton(8,   152, 148, 40, "Set Temp");
  drawButton(164, 152, 148, 40, "Set Flow");

  if (testMode)
    drawButton(60, 200, 200, 36, "TEST MODE (hold 3s)", 0x8400);
  else if (alarmSilent)
    drawButton(86, 200, 148, 36, "Unmute Alarm", COLOR_ACCENT);
  else
    drawButton(86, 200, 148, 36, "Mute Alarm", COLOR_BTN);
}

// ---------------------------------------------------------------------------
//  Settings screen
// ---------------------------------------------------------------------------
void drawSettingsScreen(bool isTemp) {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 320, 32, COLOR_HEADER);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(2);
  tft.drawString(isTemp ? "Set Max Temperature" : "Set Min Flow Rate", 8, 16);

  tft.fillRoundRect(60, 50, 200, 70, 12, COLOR_BTN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_BTN); tft.setTextSize(3);
  char valBuf[16];
  if (isTemp) { dtostrf(maxTemp, 4, 1, valBuf); strcat(valBuf, " C"); }
  else        { dtostrf(minFlow, 4, 2, valBuf); strcat(valBuf, " L/m"); }
  tft.drawString(valBuf, 160, 85);
  tft.setTextSize(1); tft.setTextColor(COLOR_SUBTEXT, COLOR_BTN);
  // Hint changes when flow check is disabled to make 0 = off obvious
  if (isTemp)
    tft.drawString("Max allowed temp", 160, 108);
  else
    tft.drawString(minFlow == 0.0f ? "Flow check DISABLED" : "Min required flow (0=off)", 160, 108);

  drawButton(10,  140, 110, 56, "-0.1", 0x2104);
  drawButton(130, 140,  60, 56, "-1",   0x2104);
  drawButton(200, 140,  60, 56, "+1",   0x03E0);
  drawButton(270, 140,  40, 56, "+0.1", 0x03E0);
  drawButton(60,  204, 200, 36, "Done", COLOR_ACCENT);
}

// ---------------------------------------------------------------------------
//  Touch handler — main screen
// ---------------------------------------------------------------------------
void handleTouchMain(int tx, int ty) {
  if (tx >= 8 && tx <= 156 && ty >= 152 && ty <= 192) {
    currentScreen = SCREEN_SET_TEMP;
    drawSettingsScreen(true);
  } else if (tx >= 164 && tx <= 312 && ty >= 152 && ty <= 192) {
    currentScreen = SCREEN_SET_FLOW;
    drawSettingsScreen(false);
  } else if (tx >= 60 && tx <= 260 && ty >= 200 && ty <= 236) {
    muteHolding    = true;
    muteHoldStart  = millis();
    muteReleaseCnt = 0;
  }
}

// ---------------------------------------------------------------------------
//  Hold detection — polled every 10ms
//  Short tap: mute/unmute in live mode
//  Hold 3s:   toggle test/live mode
// ---------------------------------------------------------------------------
void checkMuteHold() {
  if (!muteHolding) return;

  // 3 seconds elapsed — toggle mode, don't wait for lift-off
  if (millis() - muteHoldStart >= 3000) {
    testMode       = !testMode;
    muteHolding    = false;
    muteReleaseCnt = 0;
    if (testMode) {
      setLaser(false);
      silenceAlarm();
      alarmActive = false;
      alarmSilent = false;
    }
    tft.fillScreen(testMode ? 0x8400 : COLOR_OK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, testMode ? 0x8400 : COLOR_OK);
    tft.setTextSize(3);
    tft.drawString(testMode ? "TEST MODE ON" : "LIVE MODE ON", 160, 120);
    delay(1000);
    drawMainScreen(false);
    return;
  }

  // Not yet 3s — require RELEASE_NEEDED consecutive no-touch reads before
  // treating as genuine lift-off (suppresses XPT2046 dropout noise mid-hold)
  if (touch.touched()) {
    muteReleaseCnt = 0;
  } else {
    muteReleaseCnt++;
    if (muteReleaseCnt >= RELEASE_NEEDED) {
      muteReleaseCnt = 0;
      muteHolding    = false;
      if (!testMode) {
        alarmSilent = !alarmSilent;
        if (alarmSilent) silenceAlarm();
        drawMainScreen(false);
      }
    }
  }
}

// ---------------------------------------------------------------------------
//  Touch handler — settings screens
// ---------------------------------------------------------------------------
void handleTouchSettings(int tx, int ty, bool isTemp) {
  float step = 0.0f;
  if (ty >= 140 && ty <= 196) {
    if      (tx >=  10 && tx <= 120) step = -0.1f;
    else if (tx >= 130 && tx <= 190) step = -1.0f;
    else if (tx >= 200 && tx <= 260) step =  1.0f;
    else if (tx >= 270 && tx <= 310) step =  0.1f;
  }
  if (step != 0.0f) {
    if (isTemp) maxTemp = constrain(maxTemp + step, 10.0f, 40.0f);
    else        minFlow = constrain(minFlow + step, 0.0f,  5.0f);  // 0 = flow check disabled
    drawSettingsScreen(isTemp);
    return;
  }
  if (tx >= 60 && tx <= 260 && ty >= 204 && ty <= 240) {
    currentScreen = SCREEN_MAIN;
    drawMainScreen(false);
  }
}

// ---------------------------------------------------------------------------
//  Touch coordinate mapping
//  raw X (450..3799) -> screen X (0..319)
//  raw Y (677..3670) -> screen Y (0..239)
// ---------------------------------------------------------------------------
void mapTouch(TS_Point p, int &sx, int &sy) {
  sx = map(p.x, TS_MINX, TS_MAXX, 0, 320);
  sy = map(p.y, TS_MINY, TS_MAXY, 0, 240);
  sx = constrain(sx, 0, 319);
  sy = constrain(sy, 0, 239);
}

// ---------------------------------------------------------------------------
//  setup()
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY_PIN,       OUTPUT);
  pinMode(SPEAKER_PIN,     OUTPUT);
  setLaser(false);

  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowPulse, FALLING);
  lastFlowCalc = millis();

  sensors.begin();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(25, 39, 32, TOUCH_CS);
  touch.begin(touchSPI);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextSize(2);
  tft.drawString("K40 Cooling Monitor", 160, 100);
  tft.setTextSize(1); tft.setTextColor(COLOR_SUBTEXT, TFT_BLACK);
  tft.drawString(testMode ? "TEST MODE  hold bottom 3s to go live"
                          : "Starting in live mode...", 160, 130);
  delay(1500);
  drawMainScreen(false);
}

// ---------------------------------------------------------------------------
//  loop()
// ---------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  // Hold detection polled every 10ms
  static unsigned long lastHoldCheck = 0;
  if (currentScreen == SCREEN_MAIN && now - lastHoldCheck >= 10) {
    lastHoldCheck = now;
    checkMuteHold();
  }

  // Update sensors and redraw once per second
  static unsigned long lastSensorUpdate = 0;
  if (now - lastSensorUpdate >= 1000) {
    lastSensorUpdate = now;

    if (testMode) {
      waterTemp = 20.0f;
      flowRate  = 1.5f;
      noInterrupts(); pulseCount = 0; interrupts();
      lastFlowCalc = now;
    } else {
      updateFlowRate();
      waterTemp = readTemperature();
    }

    // Alarm logic with hysteresis — triggers at limit, clears only past deadband:
    //   temp:  triggers above maxTemp,    clears below (maxTemp - 0.5C)
    //   flow:  triggers below minFlow,    clears above (minFlow x 1.1)
    //   flow fault skipped entirely when minFlow == 0
    if (!testMode) {
      bool tempFault = waterTemp > maxTemp;
      bool flowFault = (minFlow > 0.0f) && (flowRate < minFlow);
      if (tempFault || flowFault) {
        alarmActive = true;
      } else if (alarmActive) {
        bool tempOK = waterTemp < (maxTemp - 0.5f);
        bool flowOK = (minFlow == 0.0f) || (flowRate > (minFlow * 1.1f));
        if (tempOK && flowOK) alarmActive = false;
      }
    } else {
      alarmActive = false;
    }

    if (!testMode) {
      if (alarmActive) { setLaser(false); }
      else { setLaser(true); silenceAlarm(); alarmSilent = false; }
    }

    // Full redraw only when alarm state changes; otherwise just repaint cards
    static bool lastAlarmState = false;
    if (currentScreen == SCREEN_MAIN) {
      if (alarmActive != lastAlarmState) {
        lastAlarmState = alarmActive;
        drawMainScreen(alarmActive);
      } else {
        updateValues();
      }
    }
  }

  // Alarm tone every 600ms
  if (alarmActive && !alarmSilent && now - lastAlarmTone >= 600) {
    soundAlarm(); lastAlarmTone = now;
  }

  // Touch handling — fires once per press, not continuously while held
  static bool wasHeld = false;
  bool isTouched = touch.tirqTouched() && touch.touched();
  if (isTouched && !wasHeld) {
    TS_Point raw = touch.getPoint();
    int sx, sy;
    mapTouch(raw, sx, sy);
    Serial.printf("Touch: raw(%d,%d) -> screen(%d,%d)\n", raw.x, raw.y, sx, sy);
    if (!muteHolding) delay(80);
    switch (currentScreen) {
      case SCREEN_MAIN:     handleTouchMain(sx, sy);           break;
      case SCREEN_SET_TEMP: handleTouchSettings(sx, sy, true); break;
      case SCREEN_SET_FLOW: handleTouchSettings(sx, sy, false);break;
    }
  }
  wasHeld = isTouched;
}

// =============================================================================
//  User_Setup.h — DELETE everything in that file and replace with this:
// =============================================================================
//
//  #define ST7789_DRIVER
//  #define TFT_WIDTH  240
//  #define TFT_HEIGHT 320
//
//  // Do NOT define TFT_RGB_ORDER — ST7789 on this board wants plain RGB
//
//  #define TFT_MOSI  13
//  #define TFT_MISO  12
//  #define TFT_SCLK  14
//  #define TFT_CS    15
//  #define TFT_DC     2
//  #define TFT_RST   -1
//  #define TFT_BL    21
//  #define TFT_BACKLIGHT_ON HIGH
//
//  #define USE_HSPI_PORT
//
//  // TOUCH_CS intentionally not defined here — XPT2046 is driven on its own
//  // VSPI bus by the XPT2046_Touchscreen library directly in the sketch.
//
//  #define SPI_FREQUENCY        40000000
//  #define SPI_READ_FREQUENCY    6000000
//  #define SPI_TOUCH_FREQUENCY    800000
//
//  #define LOAD_GLCD
//  #define LOAD_FONT2
//  #define LOAD_FONT4
//  #define LOAD_FONT6
//  #define LOAD_FONT7
//  #define LOAD_FONT8
//  #define LOAD_GFXFF
//  #define SMOOTH_FONT
// =============================================================================