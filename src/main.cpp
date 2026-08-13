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
constexpr uint32_t kMeasurementIntervalMs = 1000;
constexpr uint32_t kSummaryIntervalMs = 60000;
constexpr uint8_t kReadAttempts = 3;
constexpr size_t kPmMeasurementCount = 3;

// Atmospheric environment PM1.0, PM2.5, and PM10 in the HM3301 frame.
constexpr size_t kPmMeasurementOffsets[kPmMeasurementCount] = {
    10, 12, 14,
};

struct SummaryAccumulator {
  uint32_t sums[kPmMeasurementCount] = {};
  uint16_t maximums[kPmMeasurementCount] = {};
  uint16_t sensor_number = 0;
  uint8_t sample_count = 0;
};

struct MeasurementSummary {
  float average = 0.0f;
  uint16_t maximum = 0;
};

// The values to send when InfluxDB support is added later.
struct MinuteSummary {
  MeasurementSummary pm1_0;
  MeasurementSummary pm2_5;
  MeasurementSummary pm10;
  uint16_t sensor_number = 0;
  uint8_t sample_count = 0;
};

uint8_t frame[kHm3301FrameSize];
uint32_t next_measurement_ms = 0;
uint32_t next_summary_ms = 0;
SummaryAccumulator summary_accumulator;
MinuteSummary latest_summary;
bool sensor_ready = false;

void setErrorLed(bool error) {
  digitalWrite(kBlueLedPin, error ? HIGH : LOW);
}

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

void addMeasurements(SummaryAccumulator& target, const uint8_t* data) {
  target.sensor_number = readBigEndian16(data, 2);
  for (size_t i = 0; i < kPmMeasurementCount; ++i) {
    const uint16_t value = readBigEndian16(data, kPmMeasurementOffsets[i]);
    target.sums[i] += value;
    if (target.sample_count == 0 || value > target.maximums[i]) {
      target.maximums[i] = value;
    }
  }
  ++target.sample_count;
}

MinuteSummary finalizeSummary(const SummaryAccumulator& accumulator) {
  MinuteSummary result;
  result.sensor_number = accumulator.sensor_number;
  result.sample_count = accumulator.sample_count;
  if (accumulator.sample_count == 0) {
    return result;
  }

  MeasurementSummary* measurements[kPmMeasurementCount] = {
      &result.pm1_0, &result.pm2_5, &result.pm10,
  };
  for (size_t i = 0; i < kPmMeasurementCount; ++i) {
    measurements[i]->average =
        static_cast<float>(accumulator.sums[i]) / accumulator.sample_count;
    measurements[i]->maximum = accumulator.maximums[i];
  }
  return result;
}

void printMeasurementSummary(const char* label,
                             const MeasurementSummary& measurement) {
  Serial.printf("  %-5s: average=%.1f ug/m3, max=%u ug/m3\n", label,
                measurement.average,
                static_cast<unsigned int>(measurement.maximum));
}

void printSummary(uint32_t now, const MinuteSummary& data) {
  Serial.printf("\n[%lu ms] 1-minute summary (samples=%u)\n",
                static_cast<unsigned long>(now),
                static_cast<unsigned int>(data.sample_count));
  if (data.sample_count == 0) {
    Serial.println("no valid samples");
    return;
  }

  Serial.printf("sensor number: %u\n",
                static_cast<unsigned int>(data.sensor_number));
  Serial.println("mass concentration - atmospheric environment:");
  printMeasurementSummary("PM1.0", data.pm1_0);
  printMeasurementSummary("PM2.5", data.pm2_5);
  printMeasurementSummary("PM10", data.pm10);
}

void handleMinuteSummary(uint32_t now, const MinuteSummary& data) {
  printSummary(now, data);
  // A future InfluxDB POST can consume data here without changing aggregation.
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
  setErrorLed(false);

  if (!Wire.begin(kI2cSdaPin, kI2cSclPin, kI2cFrequencyHz)) {
    Serial.println("ERROR: failed to initialize I2C bus.");
    setErrorLed(true);
    return;
  }

  if (!selectHm3301I2c()) {
    Serial.println("ERROR: HM3301 did not respond. Check power and wiring.");
    setErrorLed(true);
    return;
  }

  // Allow the sensor to stop UART output and switch to I2C mode.
  delay(100);
  const uint32_t measurement_start_ms = millis();
  next_measurement_ms = measurement_start_ms;
  next_summary_ms = measurement_start_ms + kSummaryIntervalMs;
  sensor_ready = true;
  Serial.println("HM3301 initialized. The sensor needs about 30 seconds to warm up.");
}

void loop() {
  M5.update();

  if (!sensor_ready) {
    delay(100);
    return;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_summary_ms) >= 0) {
    latest_summary = finalizeSummary(summary_accumulator);
    handleMinuteSummary(now, latest_summary);
    summary_accumulator = {};
    do {
      next_summary_ms += kSummaryIntervalMs;
    } while (static_cast<int32_t>(now - next_summary_ms) >= 0);
  }

  if (static_cast<int32_t>(now - next_measurement_ms) < 0) {
    delay(10);
    return;
  }
  do {
    next_measurement_ms += kMeasurementIntervalMs;
  } while (static_cast<int32_t>(now - next_measurement_ms) >= 0);

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
    setErrorLed(true);
    return;
  }
  if (!checksum_valid) {
    Serial.printf(
        "ERROR: HM3301 checksum mismatch after 3 attempts "
        "(calculated=0x%02X, received=0x%02X).\n",
        calculateChecksum(frame), frame[kHm3301FrameSize - 1]);
    printRawFrame(frame);
    setErrorLed(true);
    return;
  }

  setErrorLed(false);
  addMeasurements(summary_accumulator, frame);
}
