#include <Arduino.h>
#include <M5Unified.h>

namespace {
constexpr uint8_t kBlueLedPin = 7;
constexpr uint32_t kBlinkIntervalMs = 1000;
}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);

  Serial.begin(115200);
  delay(100);
  Serial.println("M5Stack NanoC6 is ready.");

  pinMode(kBlueLedPin, OUTPUT);
  digitalWrite(kBlueLedPin, LOW);
}

void loop() {
  static bool led_on = false;

  M5.update();
  led_on = !led_on;
  digitalWrite(kBlueLedPin, led_on ? HIGH : LOW);
  delay(kBlinkIntervalMs);
}
