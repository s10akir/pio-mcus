#include <Arduino.h>
#include <M5Unified.h>
#include <Wire.h>

namespace {
constexpr uint8_t kBlueLedPin = 7;
constexpr int kI2cSclPin = 1;
constexpr int kI2cSdaPin = 2;
// HM3301 can return corrupted frames at the standard 100 kHz rate depending on
// the host and wiring. 20 kHz is fast enough for its 1 Hz data refresh rate and
// is the rate recommended by Seeed for this symptom.
constexpr uint32_t kI2cFrequencyHz = 20000;

constexpr uint8_t kHm3301Address = 0x40;
constexpr uint8_t kHm3301SelectI2cCommand = 0x88;
constexpr size_t kHm3301FrameSize = 29;
constexpr uint32_t kMeasurementIntervalMs = 5000;
constexpr uint32_t kWarmUpTimeMs = 30000;
constexpr uint8_t kReadAttempts = 3;

uint8_t frame[kHm3301FrameSize];
uint32_t next_measurement_ms = 0;

uint16_t readBigEndian16(const uint8_t* data, size_t index) {
  return (static_cast<uint16_t>(data[index]) << 8) | data[index + 1];
}

uint8_t calculateChecksum(const uint8_t* data) {
  uint8_t checksum = 0;
  for (size_t i = 0; i < kHm3301FrameSize - 1; ++i) {
    checksum += data[i];
  }
  return checksum;
}

bool selectHm3301I2c() {
  Wire.beginTransmission(kHm3301Address);
  Wire.write(kHm3301SelectI2cCommand);
  return Wire.endTransmission() == 0;
}

bool readHm3301Frame(uint8_t* data) {
  const size_t received = Wire.requestFrom(
      kHm3301Address, static_cast<size_t>(kHm3301FrameSize), true);
  if (received != kHm3301FrameSize) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < kHm3301FrameSize; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

void printRawFrame(const uint8_t* data) {
  Serial.print("raw frame:");
  for (size_t i = 0; i < kHm3301FrameSize; ++i) {
    Serial.printf(" %02X", data[i]);
  }
  Serial.println();
}

void printMeasurements(const uint8_t* data) {
  Serial.printf("sensor number: %u\n", readBigEndian16(data, 2));
  Serial.println("mass concentration - standard particulate matter (CF=1):");
  Serial.printf("  PM1.0 : %u ug/m3\n", readBigEndian16(data, 4));
  Serial.printf("  PM2.5 : %u ug/m3\n", readBigEndian16(data, 6));
  Serial.printf("  PM10  : %u ug/m3\n", readBigEndian16(data, 8));
  Serial.println("mass concentration - atmospheric environment:");
  Serial.printf("  PM1.0 : %u ug/m3\n", readBigEndian16(data, 10));
  Serial.printf("  PM2.5 : %u ug/m3\n", readBigEndian16(data, 12));
  Serial.printf("  PM10  : %u ug/m3\n", readBigEndian16(data, 14));
  Serial.println("particle count by minimum diameter:");
  Serial.printf("  >= 0.3 um : %u /L\n", readBigEndian16(data, 16));
  Serial.printf("  >= 0.5 um : %u /L\n", readBigEndian16(data, 18));
  Serial.printf("  >= 1.0 um : %u /L\n", readBigEndian16(data, 20));
  Serial.printf("  >= 2.5 um : %u /L\n", readBigEndian16(data, 22));
  Serial.printf("  >= 5.0 um : %u /L\n", readBigEndian16(data, 24));
  Serial.printf("  >= 10  um : %u /L\n", readBigEndian16(data, 26));
}
}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);

  Serial.begin(115200);
  delay(100);
  Serial.println("\nM5Stack NanoC6 + HM3301 sensor test");
  Serial.printf("I2C: SCL=G%d, SDA=G%d, address=0x%02X\n", kI2cSclPin,
                kI2cSdaPin, kHm3301Address);

  pinMode(kBlueLedPin, OUTPUT);
  digitalWrite(kBlueLedPin, LOW);

  if (!Wire.begin(kI2cSdaPin, kI2cSclPin, kI2cFrequencyHz)) {
    Serial.println("ERROR: failed to initialize I2C bus.");
    return;
  }

  if (!selectHm3301I2c()) {
    Serial.println("ERROR: HM3301 did not respond. Check power and wiring.");
    return;
  }

  // Allow the sensor to stop UART output and switch to I2C mode.
  delay(100);
  next_measurement_ms = millis() + 1000;
  Serial.println("HM3301 initialized. The sensor needs about 30 seconds to warm up.");
}

void loop() {
  M5.update();

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_measurement_ms) < 0) {
    delay(10);
    return;
  }
  next_measurement_ms = now + kMeasurementIntervalMs;

  bool read_succeeded = false;
  bool checksum_valid = false;
  for (uint8_t attempt = 0; attempt < kReadAttempts; ++attempt) {
    read_succeeded = readHm3301Frame(frame);
    checksum_valid =
        read_succeeded &&
        calculateChecksum(frame) == frame[kHm3301FrameSize - 1];
    if (checksum_valid) {
      break;
    }
    delay(50);
  }

  if (!read_succeeded) {
    Serial.println("ERROR: failed to read 29 bytes from HM3301 after 3 attempts.");
    return;
  }
  if (!checksum_valid) {
    Serial.printf(
        "ERROR: HM3301 checksum mismatch after 3 attempts "
        "(calculated=0x%02X, received=0x%02X).\n",
        calculateChecksum(frame), frame[kHm3301FrameSize - 1]);
    printRawFrame(frame);
    return;
  }

  Serial.printf("\n[%lu ms]%s\n", static_cast<unsigned long>(now),
                now < kWarmUpTimeMs ? " (warming up)" : "");
  printMeasurements(frame);

  static bool led_on = false;
  led_on = !led_on;
  digitalWrite(kBlueLedPin, led_on ? HIGH : LOW);
}
