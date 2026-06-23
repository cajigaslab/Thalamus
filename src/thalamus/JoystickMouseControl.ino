const int xPin = A0;
const int yPin = A1;

// centers (will be measured)
int xCenter = 0;
int yCenter = 0;

// deadzone
const int deadzone = 3;

// smoothing strength
const float alpha = 0.15;

// filtered values
float fx = 0;
float fy = 0;

int applyDeadzone(int value, int center, int dz) {
  if (abs(value - center) <= dz) {
    return center;
  }
  return value;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {;}

  // 🔹 Calibrate center
  const int samples = 100;
  long xSum = 0;
  long ySum = 0;

  Serial.println("Calibrating... keep joystick centered");

  for (int i = 0; i < samples; i++) {
    xSum += analogRead(xPin);
    ySum += analogRead(yPin);
    delay(5);
  }

  xCenter = xSum / samples;
  yCenter = ySum / samples;

  // initialize filter with center
  fx = xCenter;
  fy = yCenter;

  Serial.print("Center X: ");
  Serial.println(xCenter);
  Serial.print("Center Y: ");
  Serial.println(yCenter);

  Serial.println("Calibration done.");
}

void loop() {
  int xRaw = analogRead(xPin);
  int yRaw = analogRead(yPin);

  // EMA filter
  fx = fx + alpha * (xRaw - fx);
  fy = fy + alpha * (yRaw - fy);

  int xFiltered = (int)(fx + 0.5);
  int yFiltered = (int)(fy + 0.5);

  // apply deadzone
  xFiltered = applyDeadzone(xFiltered, xCenter, deadzone);
  yFiltered = applyDeadzone(yFiltered, yCenter, deadzone);

  Serial.print(xFiltered);
  Serial.print(",");
  Serial.println(yFiltered);

  delay(5);
}
