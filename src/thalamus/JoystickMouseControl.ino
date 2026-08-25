// Joystick serial firmware for the Thalamus JOYSTICK node.
//
// Contract with thalamus/src/thalamus/joystick_node.cpp:
//   - streams one "x,y\n" line of 10-bit ADC values (0..1023) at 115200 baud
//   - the node re-centers/normalizes as: out = (value - Center) / 512, with a
//     dead zone snap when |value - Center| <= Dead Zone.
//
// Robust-centering strategy: instead of letting the node's hardcoded X/Y Center
// track this board's electrical rest point (which drifts boot-to-boot with
// temperature/mechanics), we measure rest here every boot and RE-BIAS the output
// so that rest ALWAYS maps to 512. That lets eevee.json use a fixed, permanently
// correct Center of 512/512 and a small dead zone equal to just the noise band.
//
// Any non-"x,y" line (the "# ..." banners below) is ignored by the node parser.

const int xPin = A0;
const int yPin = A1;

// LED feedback is optional: the Pro Micro (ATmega32u4) has no pin-13 LED and
// some board packages leave LED_BUILTIN undefined, so guard every use of it.
#ifdef LED_BUILTIN
  #define LED_INIT()  pinMode(LED_BUILTIN, OUTPUT)
  #define LED_SET(v)  digitalWrite(LED_BUILTIN, (v))
#else
  #define LED_INIT()
  #define LED_SET(v)
#endif

// ---- calibration tuning -------------------------------------------------------
const unsigned long SETTLE_MS   = 500;  // discard readings right after boot
const int   CAL_WINDOW          = 60;   // samples per calibration attempt
const int   CAL_SAMPLE_DELAY_MS = 4;    // spacing between calibration samples
const int   CAL_STABLE_P2P      = 12;   // max peak-to-peak (ADC counts) to accept
const int   CAL_MAX_ATTEMPTS    = 25;   // ~ several seconds before best-effort

// ---- output tuning ------------------------------------------------------------
const int   OVERSAMPLE   = 4;     // reads averaged per axis per output (kills white noise)
const float ALPHA        = 0.40;  // light EMA; higher = snappier, lower = smoother
const int   OUTPUT_DELAY_MS = 5;  // ~200 Hz, matches the node's 5 ms sample interval
// NOTE: no dead zone is applied here on purpose. The Thalamus node owns the dead
// zone so there is a single source of truth (no double dead zone).

// measured electrical rest, filled by calibrate()
int xCenter = 512;
int yCenter = 512;

// EMA state
float fx = 512.0;
float fy = 512.0;

static int median_of(int *buf, int n) {
  // simple insertion sort (n is small, runs only during calibration)
  for (int i = 1; i < n; i++) {
    int key = buf[i];
    int j = i - 1;
    while (j >= 0 && buf[j] > key) {
      buf[j + 1] = buf[j];
      j--;
    }
    buf[j + 1] = key;
  }
  return buf[n / 2];
}

// Collect a stable window at rest and lock the center to its median.
// Retries while the stick is being touched/moving; keeps the best (least noisy)
// attempt as a fallback so we never hang forever.
void calibrate() {
  static int xbuf[CAL_WINDOW];
  static int ybuf[CAL_WINDOW];

  int bestP2P = 32767;
  int bestXCenter = 512, bestYCenter = 512;

  for (int attempt = 1; attempt <= CAL_MAX_ATTEMPTS; attempt++) {
    int xmin = 1023, xmax = 0, ymin = 1023, ymax = 0;

    for (int i = 0; i < CAL_WINDOW; i++) {
      int xr = analogRead(xPin);
      int yr = analogRead(yPin);
      xbuf[i] = xr; ybuf[i] = yr;
      if (xr < xmin) xmin = xr;
      if (xr > xmax) xmax = xr;
      if (yr < ymin) ymin = yr;
      if (yr > ymax) ymax = yr;
      LED_SET((i & 8) ? HIGH : LOW);  // blink = calibrating
      delay(CAL_SAMPLE_DELAY_MS);
    }

    int p2pX = xmax - xmin;
    int p2pY = ymax - ymin;
    int p2p  = (p2pX > p2pY) ? p2pX : p2pY;

    if (p2p < bestP2P) {
      bestP2P = p2p;
      bestXCenter = median_of(xbuf, CAL_WINDOW);
      bestYCenter = median_of(ybuf, CAL_WINDOW);
    }

    if (p2pX <= CAL_STABLE_P2P && p2pY <= CAL_STABLE_P2P) {
      xCenter = median_of(xbuf, CAL_WINDOW);
      yCenter = median_of(ybuf, CAL_WINDOW);
      Serial.print("# center locked: X="); Serial.print(xCenter);
      Serial.print(" Y="); Serial.print(yCenter);
      Serial.print(" (attempt "); Serial.print(attempt);
      Serial.print(", p2p X="); Serial.print(p2pX);
      Serial.print(" Y="); Serial.print(p2pY); Serial.println(")");
      return;
    }

    Serial.print("# unstable (p2p X="); Serial.print(p2pX);
    Serial.print(" Y="); Serial.print(p2pY);
    Serial.println(") - is the stick being touched? retrying");
  }

  // best-effort fallback
  xCenter = bestXCenter;
  yCenter = bestYCenter;
  Serial.print("# center best-effort: X="); Serial.print(xCenter);
  Serial.print(" Y="); Serial.print(yCenter);
  Serial.print(" (best p2p="); Serial.print(bestP2P); Serial.println(")");
}

void setup() {
  LED_INIT();
  Serial.begin(115200);
  while (!Serial) {;}

  unsigned long t0 = millis();
  while (millis() - t0 < SETTLE_MS) { analogRead(xPin); analogRead(yPin); }

  Serial.println("# calibrating - keep the joystick centered and untouched");
  calibrate();

  fx = xCenter;
  fy = yCenter;

  LED_SET(HIGH);  // solid = streaming
  Serial.println("# streaming (rest re-biased to 512)");
}

void loop() {
  long xacc = 0, yacc = 0;
  for (int i = 0; i < OVERSAMPLE; i++) {
    xacc += analogRead(xPin);
    yacc += analogRead(yPin);
  }
  float xRaw = (float)xacc / OVERSAMPLE;
  float yRaw = (float)yacc / OVERSAMPLE;

  // light EMA
  fx += ALPHA * (xRaw - fx);
  fy += ALPHA * (yRaw - fy);

  // re-bias so measured rest -> 512, then clamp to the ADC range
  long xOut = (long)(fx + 0.5) - xCenter + 512;
  long yOut = (long)(fy + 0.5) - yCenter + 512;
  xOut = constrain(xOut, 0, 1023);
  yOut = constrain(yOut, 0, 1023);

  Serial.print(xOut);
  Serial.print(",");
  Serial.println(yOut);

  delay(OUTPUT_DELAY_MS);
}
