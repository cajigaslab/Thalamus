const int xPin = A0;
const int yPin = A1;

// measured centers
const int xCenter = 516;
const int yCenter = 514;

// deadzone
const int deadzone = 3;

// smoothing strength
const float alpha = 0.15;

// filtered values
float fx = xCenter;
float fy = yCenter;

int applyDeadzone(int value, int center, int dz) {
  if (abs(value - center) <= dz) {
    return center;
  }
  return value;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {;}
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

  // send ONLY filtered values
  Serial.print(xFiltered);
  Serial.print(",");
  Serial.println(yFiltered);

  delay(5);
}
