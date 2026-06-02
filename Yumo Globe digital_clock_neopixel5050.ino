/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║                                                                  ║
 * ║                      YUMO  BUILDS                                ║
 * ║                    —  DIGITAL CLOCK  —                           ║
 * ║                                                                  ║
 * ║   ESP32-C3 Super Mini  |  v2.5                                   ║
 * ║   NTP Time Sync · Auto Timezone · Startup Animation              ║
 * ║   SKC6812RV x4 base ground LEDs + x1 top side LED on GPIO21     ║
 * ║                                                                  ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * ── HARDWARE ────────────────────────────────────────────────────────
 *
 *   MCU      ESP32-C3 Super Mini
 *   DISPLAY  LTD-4608E  4-digit 7-segment, Common Anode
 *   LEDS     5x SKC6812RV on GPIO21
 *            LED 0-3 = base ground ring
 *            LED 4   = top side light
 *
 * ── PIN WIRING MAP ──────────────────────────────────────────────────
 *
 *   Segments (220Ω → display pin, ACTIVE LOW):
 *   PIN  6   SEG A        PIN  7   SEG B
 *   PIN  5   SEG C        PIN  0   SEG D
 *   PIN  2   SEG E        PIN  4   SEG F
 *   PIN  3   SEG G        PIN  1   SEG DP  (colon)
 *
 *   Common Anodes (ACTIVE HIGH):
 *   PIN  8   CA1          PIN  9   CA2
 *   PIN 10   CA3          PIN 20   CA4
 *
 *   LED chain GPIO21:
 *   LED0 → LED1 → LED2 → LED3 (base ring) → LED4 (top side)
 *   Power    3.7–4.2V from 18650 parallel pack
 *   Ground   shared with ESP32 GND
 *
 * ── FULL STARTUP SEQUENCE ───────────────────────────────────────────
 *
 *   All 5 LEDs sync during startup animation
 *   After clock starts → base LEDs off, top LED solos:
 *   Breathes neon yellow → purple → blue → red → white
 *   2 cycles per colour, 2 seconds per breath, then stops
 *
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <time.h>
#include <FastLED.h>

// ── 7-segment pins ───────────────────────────────────────────────────
#define SEG_A   6
#define SEG_B   7
#define SEG_C   5
#define SEG_D   0
#define SEG_E   2
#define SEG_F   4
#define SEG_G   3
#define SEG_DP  1

#define CA1   8
#define CA2   9
#define CA3  10
#define CA4  20

// ── LED chain ────────────────────────────────────────────────────────
#define LED_PIN      21
#define NUM_LEDS     5       // 4 base + 1 top
#define LED_BASE     4       // how many base LEDs
#define LED_TOP      4       // index of top LED
#define LED_TYPE     SK6812
#define COLOR_ORDER  GRB

CRGB leds[NUM_LEDS];

// ── Display ──────────────────────────────────────────────────────────
#define POS1 1
#define POS2 0
#define POS3 3
#define POS4 2

const int segPins[8] = {SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G, SEG_DP};
const int caPins[4]  = {CA1, CA2, CA3, CA4};

const byte digitFont[10] = {
  0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
  0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111
};
#define SEG_BLANK 0b00000000
#define SEG_DASH  0b01000000

volatile byte dispBuf[4] = {SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH};
volatile bool colonOn     = false;

hw_timer_t  *muxTimer = NULL;
portMUX_TYPE muxMux   = portMUX_INITIALIZER_UNLOCKED;

// ── Mux ISR ──────────────────────────────────────────────────────────
void IRAM_ATTR onMuxTimer() {
  static int d = 0;
  portENTER_CRITICAL_ISR(&muxMux);
  digitalWrite(caPins[d], LOW);
  d = (d + 1) % 4;
  byte seg = dispBuf[d];
  for (int i = 0; i < 7; i++)
    digitalWrite(segPins[i], (seg >> i) & 1 ? LOW : HIGH);
  digitalWrite(SEG_DP, (d == 0 && colonOn) ? LOW : HIGH);
  digitalWrite(caPins[d], HIGH);
  portEXIT_CRITICAL_ISR(&muxMux);
}

void startMuxTimer() {
  muxTimer = timerBegin(1000000);
  timerAttachInterrupt(muxTimer, &onMuxTimer);
  timerAlarm(muxTimer, 3000, true, 0);
}

// ── LED helpers ───────────────────────────────────────────────────────

void ledOff() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// Set only the 4 base LEDs, leave top untouched
void setBase(CRGB c) {
  for (int i = 0; i < LED_BASE; i++) leds[i] = c;
  FastLED.show();
}

// Fade base LEDs from A to B over durationMs, top LED untouched
void fadeBase(CRGB colorA, CRGB colorB, unsigned long durationMs) {
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    float t = (float)(millis() - start) / (float)durationMs;
    CRGB  c = blend(colorA, colorB, (uint8_t)(t * 255));
    for (int i = 0; i < LED_BASE; i++) leds[i] = c;
    FastLED.show();
    delay(16);
  }
  for (int i = 0; i < LED_BASE; i++) leds[i] = colorB;
  FastLED.show();
}

// Countdown rainbow
const CRGB countdownColors[11] = {
  CRGB(255, 0,   0),
  CRGB(255, 80,  0),
  CRGB(255, 180, 0),
  CRGB(200, 255, 0),
  CRGB(0,   255, 0),
  CRGB(0,   255, 150),
  CRGB(0,   200, 255),
  CRGB(0,   80,  255),
  CRGB(80,  0,   255),
  CRGB(180, 0,   255),
  CRGB(255, 0,   180),
};

// ── Top LED solo breathing after clock starts ─────────────────────────
// 5 neon colours × 2 cycles × 2 seconds per breath = 20 seconds total
void topLedSolo() {
  const CRGB soloColors[5] = {
    CRGB(255, 230, 0),    // neon yellow
    CRGB(180, 0,   255),  // purple
    CRGB(0,   100, 255),  // neon blue
    CRGB(255, 0,   50),   // neon red
    CRGB(255, 255, 255),  // white
  };

  // Make sure base LEDs are off, only top LED active
  for (int i = 0; i < LED_BASE; i++) leds[i] = CRGB::Black;

  int cyclesPerColor = 2;
  unsigned long breathMs = 2000; // 2 seconds per breath (low→high→low)

  for (int c = 0; c < 5; c++) {
    for (int cycle = 0; cycle < cyclesPerColor; cycle++) {
      unsigned long breathStart = millis();
      while (millis() - breathStart < breathMs) {
        float   progress   = (float)(millis() - breathStart) / (float)breathMs * 2.0f * PI;
        uint8_t brightness = (uint8_t)((sin(progress - PI / 2.0f) * 0.5f + 0.5f) * 255.0f);
        leds[LED_TOP] = soloColors[c];
        leds[LED_TOP].nscale8(brightness < 5 ? 5 : brightness);
        FastLED.show();
        delay(16);
      }
    }
  }

  // All done — top LED off
  leds[LED_TOP] = CRGB::Black;
  FastLED.show();
}

// ── Display helpers ───────────────────────────────────────────────────
void allOff() {
  for (int i = 0; i < 4; i++) digitalWrite(caPins[i],  LOW);
  for (int i = 0; i < 8; i++) digitalWrite(segPins[i], HIGH);
}
void allDigitsOn() { for (int i = 0; i < 4; i++) digitalWrite(caPins[i], HIGH); }
void allSegsOn()   { for (int i = 0; i < 7; i++) digitalWrite(segPins[i], LOW); }

// ── Full startup animation ────────────────────────────────────────────
void startupAnimation() {

  // ── SPINNER — warm fade red → orange → gold, top LED mirrors base ──
  CRGB warmPalette[] = {
    CRGB(180, 0,   0),
    CRGB(220, 60,  0),
    CRGB(255, 140, 0),
    CRGB(200, 180, 0),
  };
  int warmSize = 4;

  const int spinner[6]  = {SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F};
  unsigned long spinDur  = 3 * 6 * 70;
  unsigned long spinStart = millis();

  for (int r = 0; r < 3; r++) {
    for (int s = 0; s < 6; s++) {
      allOff(); allDigitsOn();
      digitalWrite(spinner[s], LOW);

      unsigned long elapsed = millis() - spinStart;
      int   ci = min((int)(elapsed / (spinDur / (warmSize - 1))), warmSize - 2);
      float t  = (float)(elapsed - ci * (spinDur / (warmSize - 1))) / (float)(spinDur / (warmSize - 1));
      CRGB  c  = blend(warmPalette[ci], warmPalette[ci + 1], (uint8_t)(t * 255));
      fill_solid(leds, NUM_LEDS, c); // all 5 LEDs including top
      FastLED.show();
      delay(70);
    }
  }

  // ── FILL — cool fade teal → cyan → blue ───────────────────────────
  CRGB coolPalette[] = {
    CRGB(0,   180, 160),
    CRGB(0,   220, 200),
    CRGB(0,   180, 255),
    CRGB(40,  100, 255),
  };
  int coolSize = 4;

  allOff(); allDigitsOn();
  const int fill[8]      = {SEG_A, SEG_F, SEG_B, SEG_G, SEG_E, SEG_C, SEG_D, SEG_DP};
  unsigned long fillDur   = 8 * 80 + 400;
  unsigned long fillStart = millis();

  for (int s = 0; s < 8; s++) {
    digitalWrite(fill[s], LOW);
    unsigned long elapsed = millis() - fillStart;
    int   ci = min((int)(elapsed / (fillDur / (coolSize - 1))), coolSize - 2);
    float t  = (float)(elapsed - ci * (fillDur / (coolSize - 1))) / (float)(fillDur / (coolSize - 1));
    CRGB  c  = blend(coolPalette[ci], coolPalette[ci + 1], (uint8_t)(t * 255));
    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(80);
  }

  unsigned long holdStart = millis();
  while (millis() - holdStart < 400) {
    unsigned long elapsed = millis() - fillStart;
    int   ci = min((int)(elapsed / (fillDur / (coolSize - 1))), coolSize - 2);
    float t  = (float)(elapsed - ci * (fillDur / (coolSize - 1))) / (float)(fillDur / (coolSize - 1));
    CRGB  c  = blend(coolPalette[ci], coolPalette[min(ci + 1, coolSize - 1)], (uint8_t)(t * 255));
    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(16);
  }

  // ── 3x FLASH — all 5 LEDs flash white in sync ─────────────────────
  for (int f = 0; f < 3; f++) {
    allDigitsOn(); allSegsOn();
    fill_solid(leds, NUM_LEDS, CRGB(220, 220, 220));
    FastLED.show();
    delay(120);
    allOff();
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(120);
  }

  // ── PAIR WIPE — all 5 fade gold ───────────────────────────────────
  allOff(); allSegsOn();
  digitalWrite(caPins[POS1], HIGH);
  digitalWrite(caPins[POS2], HIGH);
  fadeBase(CRGB::Black, CRGB(255, 180, 0), 350);
  leds[LED_TOP] = CRGB(255, 180, 0); FastLED.show();
  digitalWrite(caPins[POS1], LOW);
  digitalWrite(caPins[POS2], LOW);

  allOff(); allSegsOn();
  digitalWrite(caPins[POS3], HIGH);
  digitalWrite(caPins[POS4], HIGH);
  fadeBase(CRGB(255, 180, 0), CRGB(255, 120, 0), 350);
  leds[LED_TOP] = CRGB(255, 120, 0); FastLED.show();
  digitalWrite(caPins[POS3], LOW);
  digitalWrite(caPins[POS4], LOW);

  allOff(); delay(200); allSegsOn();
  digitalWrite(caPins[POS1], HIGH);
  digitalWrite(caPins[POS4], HIGH);
  fadeBase(CRGB(255, 120, 0), CRGB(255, 200, 50), 500);
  leds[LED_TOP] = CRGB(255, 200, 50); FastLED.show();
  digitalWrite(caPins[POS1], LOW);
  digitalWrite(caPins[POS4], LOW);
  allOff();

  fadeBase(CRGB(255, 200, 50), CRGB::Black, 300);
  leds[LED_TOP] = CRGB::Black; FastLED.show();

  // ── DOT CHASE — all 5 LEDs in purple tones ────────────────────────
  const CRGB dotColors[4] = {
    CRGB(80,  0,   200),
    CRGB(120, 0,   255),
    CRGB(60,  0,   180),
    CRGB(150, 0,   255),
  };
  const int dotOrder[4] = {POS1, POS2, POS3, POS4};

  for (int r = 0; r < 3; r++) {
    allOff();
    for (int d = 0; d < 4; d++) {
      digitalWrite(SEG_DP, LOW);
      digitalWrite(caPins[dotOrder[d]], HIGH);
      if (d > 0) leds[d - 1].nscale8(60);
      leds[d]      = dotColors[d];
      leds[LED_TOP] = dotColors[d]; // top mirrors current dot colour
      FastLED.show();
      delay(300);
    }
    for (int fade = 255; fade >= 0; fade -= 30) {
      for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(fade > 0 ? fade : 1);
      FastLED.show();
      delay(20);
    }
    ledOff();
    delay(200);
  }
  allOff();
  delay(300);

  // ── COUNTDOWN 10→0 — all 5 LEDs breathe rainbow per number ────────
  for (int n = 10; n >= 0; n--) {
    byte segL     = (n == 10) ? digitFont[1] : SEG_BLANK;
    byte segR     = digitFont[n % 10];
    int  colorIdx = 10 - n;

    unsigned long stepStart = millis();
    unsigned long stepDur   = 700;

    while (millis() - stepStart < stepDur) {
      for (int i = 0; i < 7; i++) digitalWrite(segPins[i], (segL >> i) & 1 ? LOW : HIGH);
      digitalWrite(SEG_DP, HIGH);
      digitalWrite(caPins[POS2], HIGH); delayMicroseconds(2000); digitalWrite(caPins[POS2], LOW);
      for (int i = 0; i < 7; i++) digitalWrite(segPins[i], (segR >> i) & 1 ? LOW : HIGH);
      digitalWrite(SEG_DP, HIGH);
      digitalWrite(caPins[POS3], HIGH); delayMicroseconds(2000); digitalWrite(caPins[POS3], LOW);

      float   progress   = (float)(millis() - stepStart) / (float)stepDur * 2.0f * PI;
      uint8_t brightness = (uint8_t)((sin(progress) * 0.5f + 0.5f) * 255.0f);
      CRGB    c          = countdownColors[colorIdx];
      c.nscale8(brightness < 20 ? 20 : brightness);
      fill_solid(leds, NUM_LEDS, c); // all 5 in sync
      FastLED.show();
    }
  }

  ledOff();
}

// ── Post countdown: clockwise + dance + off (base only) ───────────────
void ledPostCountdown() {
  const CRGB oneByOneColors[4] = {
    CRGB(255, 0,   0),
    CRGB(0,   255, 0),
    CRGB(0,   0,   255),
    CRGB(255, 255, 0),
  };

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  for (int i = 0; i < LED_BASE; i++) {
    leds[i] = oneByOneColors[i];
    FastLED.show();
    delay(250);
  }
  delay(300);

  const CRGB danceColors[4] = {
    CRGB(0,   0,   255),
    CRGB(80,  0,   255),
    CRGB(150, 0,   255),
    CRGB(0,   150, 255),
  };

  unsigned long danceStart = millis();
  int step = 0;
  while (millis() - danceStart < 4000) {
    for (int i = 0; i < LED_BASE; i++) {
      leds[i] = danceColors[(i + step) % 4];
      if ((i + step) % 2 == 0) leds[i].nscale8(255);
      else                      leds[i].nscale8(80);
    }
    leds[LED_TOP] = CRGB::Black; // top stays off during dance
    FastLED.show();
    delay(120);
    step = (step + 1) % 4;
  }

  ledOff();
}

// ── LED5 (top): 3 super-fast flashes, each a different colour ────────
// Triggered every 30 seconds. Base LEDs untouched.
void topLedTripleFlash() {
  const CRGB flashColors[3] = {
    CRGB(0,   255, 180),   // neon cyan-green
    CRGB(255, 0,   200),   // hot magenta
    CRGB(255, 180, 0),     // electric amber
  };
  for (int f = 0; f < 3; f++) {
    leds[LED_TOP] = flashColors[f];
    FastLED.show();
    delay(40);                       // super-fast ON
    leds[LED_TOP] = CRGB::Black;
    FastLED.show();
    delay(60);                       // super-fast OFF gap
  }
}

// ── Hourly show: all 5 LEDs futuristic neon sweep ─────────────────────
// ~6 seconds long. Runs at the top of every hour.
void hourlyNeonShow() {
  const CRGB neon[6] = {
    CRGB(0,   255, 255),   // cyan
    CRGB(180, 0,   255),   // violet
    CRGB(0,   255, 80),    // neon green
    CRGB(255, 20,  147),   // deep pink
    CRGB(0,   120, 255),   // electric blue
    CRGB(255, 230, 0),     // neon yellow
  };

  // ── Phase 1: fast colour chase across all 5 LEDs (1.5 s) ───────────
  for (int rep = 0; rep < 3; rep++) {
    for (int c = 0; c < 6; c++) {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      leds[c % NUM_LEDS] = neon[c];          // light one LED at a time
      FastLED.show();
      delay(80);
    }
  }

  // ── Phase 2: all 5 breathe each neon colour once (3 s) ──────────────
  for (int c = 0; c < 6; c++) {
    unsigned long breathStart = millis();
    unsigned long breathMs    = 500;          // 0.5 s per colour breath
    while (millis() - breathStart < breathMs) {
      float   prog       = (float)(millis() - breathStart) / (float)breathMs * 2.0f * PI;
      uint8_t brightness = (uint8_t)((sin(prog - PI / 2.0f) * 0.5f + 0.5f) * 255.0f);
      CRGB    col        = neon[c];
      col.nscale8(brightness < 10 ? 10 : brightness);
      fill_solid(leds, NUM_LEDS, col);
      FastLED.show();
      delay(16);
    }
  }

  // ── Phase 3: rapid strobe white flash × 5 then fade out (1.5 s) ────
  for (int f = 0; f < 5; f++) {
    fill_solid(leds, NUM_LEDS, CRGB(220, 220, 255));
    FastLED.show();
    delay(50);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(50);
  }

  // Leave all LEDs off after the show
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// ── Every 5 min: alternating slow breath — base vs top ────────────────
//
//  Full ~5-minute sequence of 8 colour pairs, all in blue/purple/white/gold.
//  Each pair:
//    BASE (1-4): breathe in 3s → hold 1.5s → breathe out 3s   [top OFF]
//    GAP: 0.5 s silence
//    TOP  (5):   breathe in 4s → hold 4s   → breathe out 4s   [base OFF]
//    GAP: 0.5 s silence
//
//  8 pairs × ~20 s each ≈ 160 s core + transitions ≈ ~5 min total.
//
void alternatingBreath() {

  // ── Palette: 8 pairs, base and top always contrasting ─────────────────
  //   All colours stay inside blue / purple / white / gold family.
  const CRGB baseCol[8] = {
    CRGB(0,   80,  255),   // deep royal blue
    CRGB(255, 200, 20),    // warm gold
    CRGB(120, 0,   255),   // rich purple
    CRGB(220, 220, 255),   // soft cold white
    CRGB(0,   40,  200),   // dark navy blue
    CRGB(200, 160, 0),     // deep amber gold
    CRGB(80,  0,   200),   // deep violet
    CRGB(255, 240, 180),   // warm cream white
  };
  const CRGB topCol[8] = {
    CRGB(255, 210, 0),     // electric gold
    CRGB(60,  0,   220),   // cobalt blue
    CRGB(200, 200, 255),   // icy white-blue
    CRGB(160, 0,   255),   // violet purple
    CRGB(255, 230, 80),    // bright gold
    CRGB(0,   60,  255),   // vivid blue
    CRGB(240, 240, 255),   // pure cool white
    CRGB(100, 0,   240),   // deep indigo
  };

  // Timing (milliseconds)
  const unsigned long BASE_IN   = 3000;
  const unsigned long BASE_HOLD = 1500;
  const unsigned long BASE_OUT  = 3000;
  const unsigned long TOP_IN    = 4000;
  const unsigned long TOP_HOLD  = 4000;   // long linger on top
  const unsigned long TOP_OUT   = 4000;
  const unsigned long GAP       = 500;

  for (int p = 0; p < 8; p++) {

    CRGB bC = baseCol[p];
    CRGB tC = topCol[p];

    // ── BASE breath (top stays off) ──────────────────────────────────
    // In
    unsigned long t0 = millis();
    while (millis() - t0 < BASE_IN) {
      float   frac = (float)(millis() - t0) / (float)BASE_IN;
      uint8_t bri  = (uint8_t)(frac * 255.0f);
      for (int i = 0; i < LED_BASE; i++) { leds[i] = bC; leds[i].nscale8(bri < 4 ? 4 : bri); }
      leds[LED_TOP] = CRGB::Black;
      FastLED.show(); delay(16);
    }
    // Hold
    for (int i = 0; i < LED_BASE; i++) leds[i] = bC;
    leds[LED_TOP] = CRGB::Black;
    FastLED.show();
    delay(BASE_HOLD);
    // Out
    t0 = millis();
    while (millis() - t0 < BASE_OUT) {
      float   frac = 1.0f - (float)(millis() - t0) / (float)BASE_OUT;
      uint8_t bri  = (uint8_t)(frac * 255.0f);
      for (int i = 0; i < LED_BASE; i++) { leds[i] = bC; leds[i].nscale8(bri < 4 ? 4 : bri); }
      leds[LED_TOP] = CRGB::Black;
      FastLED.show(); delay(16);
    }
    // Gap
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(GAP);

    // ── TOP breath (base stays off) ───────────────────────────────────
    // In
    t0 = millis();
    while (millis() - t0 < TOP_IN) {
      float   frac = (float)(millis() - t0) / (float)TOP_IN;
      uint8_t bri  = (uint8_t)(frac * 255.0f);
      for (int i = 0; i < LED_BASE; i++) leds[i] = CRGB::Black;
      leds[LED_TOP] = tC; leds[LED_TOP].nscale8(bri < 4 ? 4 : bri);
      FastLED.show(); delay(16);
    }
    // Long hold — top lingers heavily
    for (int i = 0; i < LED_BASE; i++) leds[i] = CRGB::Black;
    leds[LED_TOP] = tC;
    FastLED.show();
    delay(TOP_HOLD);
    // Out
    t0 = millis();
    while (millis() - t0 < TOP_OUT) {
      float   frac = 1.0f - (float)(millis() - t0) / (float)TOP_OUT;
      uint8_t bri  = (uint8_t)(frac * 255.0f);
      for (int i = 0; i < LED_BASE; i++) leds[i] = CRGB::Black;
      leds[LED_TOP] = tC; leds[LED_TOP].nscale8(bri < 4 ? 4 : bri);
      FastLED.show(); delay(16);
    }
    // Gap
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(GAP);
  }

  // All off at end
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// ── Time sync ─────────────────────────────────────────────────────────
void syncTime() {
  if (WiFi.status() != WL_CONNECTED) return;
  long offset = 0;
  HTTPClient http;
  http.begin("http://ip-api.com/line/?fields=offset");
  http.setTimeout(8000);
  if (http.GET() == HTTP_CODE_OK) offset = http.getString().toInt();
  http.end();
  configTime(offset, 0, "pool.ntp.org", "time.google.com");
}

// ── Setup ─────────────────────────────────────────────────────────────
void setup() {
  for (int i = 0; i < 8; i++) { pinMode(segPins[i], OUTPUT); digitalWrite(segPins[i], HIGH); }
  for (int i = 0; i < 4; i++) { pinMode(caPins[i],  OUTPUT); digitalWrite(caPins[i],  LOW);  }

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(180);
  ledOff();

  // Startup animation (all 5 LEDs in sync with display)
  startupAnimation();

  // Post countdown (base LEDs only)
  ledPostCountdown();

  // Clock display starts here
  startMuxTimer();

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.autoConnect("YumoClock");

  delay(1000);
  syncTime();

  struct tm t;
  while (!getLocalTime(&t)) { delay(200); }

  // Top LED solos after clock is running
  topLedSolo();
}

// ── Loop ──────────────────────────────────────────────────────────────
void loop() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 100) {
    lastUpdate = millis();
    struct tm t;
    if (getLocalTime(&t)) {
      portENTER_CRITICAL(&muxMux);
      dispBuf[POS1] = digitFont[t.tm_hour / 10];
      dispBuf[POS2] = digitFont[t.tm_hour % 10];
      dispBuf[POS3] = digitFont[t.tm_min  / 10];
      dispBuf[POS4] = digitFont[t.tm_min  % 10];
      portEXIT_CRITICAL(&muxMux);
    }
  }

  static unsigned long lastBlink = 0;
  if (millis() - lastBlink >= 500) { lastBlink = millis(); colonOn = !colonOn; }

  // ── LED5: triple-flash every 30 seconds ──────────────────────────────
  static unsigned long lastTripleFlash = 0;
  if (millis() - lastTripleFlash >= 30000UL) {
    lastTripleFlash = millis();
    topLedTripleFlash();
  }

  // ── Hourly neon show for all 5 LEDs ──────────────────────────────────
  static unsigned long lastHourlyShow = 0;
  if (millis() - lastHourlyShow >= 3600000UL) {
    lastHourlyShow = millis();
    hourlyNeonShow();
  }

  // ── Every 5 min: alternating slow breath base ↔ top ──────────────────
  static unsigned long lastAlternatingBreath = 0;
  if (millis() - lastAlternatingBreath >= 300000UL) {
    lastAlternatingBreath = millis();
    alternatingBreath();
  }

  static unsigned long lastSync = 0;
  if (millis() - lastSync >= 3600000UL) { lastSync = millis(); syncTime(); }

  if (WiFi.status() != WL_CONNECTED) {
    portENTER_CRITICAL(&muxMux);
    dispBuf[POS1] = dispBuf[POS2] = dispBuf[POS3] = dispBuf[POS4] = SEG_DASH;
    portEXIT_CRITICAL(&muxMux);
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.autoConnect("YumoClock");
    syncTime();
  }

  delay(50);
}