#include <Arduino.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "metrics.h"
#include "secrets.h"

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
constexpr uint32_t kWifiRetryIntervalMs = 10000;
constexpr uint8_t kReadAttempts = 3;
constexpr time_t kMinimumValidTime = 1577836800;  // 2020-01-01 UTC
constexpr size_t kPrometheusPayloadSize = 1024;

// Atmospheric environment PM1.0, PM2.5, and PM10 in the HM3301 frame.
struct PmMeasurement {
  size_t offset;
  const char* label;
};
constexpr PmMeasurement kPmMeasurements[] = {
    {10, "PM1.0"}, {12, "PM2.5"}, {14, "PM10"}};
static_assert(sizeof(kPmMeasurements) / sizeof(kPmMeasurements[0]) ==
                  kPmMeasurementCount,
              "PM measurement definitions must match the summary shape");

struct SummaryAccumulator {
  uint32_t sums[kPmMeasurementCount] = {};
  uint16_t maximums[kPmMeasurementCount] = {};
  uint16_t sensor_number = 0;
  uint8_t sample_count = 0;
};

uint8_t frame[kHm3301FrameSize];
char prometheus_payload[kPrometheusPayloadSize];
uint32_t next_measurement_ms = 0;
uint32_t next_summary_ms = 0;
uint32_t next_wifi_retry_ms = 0;
SummaryAccumulator summary_accumulator;
bool sensor_ready = false;
bool sensor_error = false;
bool network_error = true;
bool wifi_was_connected = false;

void updateErrorLed() {
  digitalWrite(kBlueLedPin, sensor_error || network_error ? HIGH : LOW);
}

void startNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(Secrets::kWifiSsid, Secrets::kWifiPassword);
  configTime(0, 0, Secrets::kNtpServer);
  next_wifi_retry_ms = millis() + kWifiRetryIntervalMs;
  Serial.printf("Wi-Fi connecting to %s\n", Secrets::kWifiSsid);
}

void maintainNetwork(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifi_was_connected) {
      Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
      wifi_was_connected = true;
    }
    return;
  }

  if (wifi_was_connected) {
    Serial.println("ERROR: Wi-Fi disconnected.");
    wifi_was_connected = false;
  }
  network_error = true;
  updateErrorLed();
  if (static_cast<int32_t>(now - next_wifi_retry_ms) >= 0) {
    Serial.println("Wi-Fi reconnecting...");
    WiFi.begin(Secrets::kWifiSsid, Secrets::kWifiPassword);
    next_wifi_retry_ms = now + kWifiRetryIntervalMs;
  }
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
    const uint16_t value = readBigEndian16(data, kPmMeasurements[i].offset);
    target.sums[i] += value;
    if (value > target.maximums[i]) {
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

  for (size_t i = 0; i < kPmMeasurementCount; ++i) {
    result.measurements[i].average =
        static_cast<float>(accumulator.sums[i]) / accumulator.sample_count;
    result.measurements[i].maximum = accumulator.maximums[i];
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
  for (size_t i = 0; i < kPmMeasurementCount; ++i) {
    printMeasurementSummary(kPmMeasurements[i].label, data.measurements[i]);
  }
}

bool sendSummary(const MinuteSummary& summary) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: summary not sent: Wi-Fi is disconnected.");
    return false;
  }
  if (time(nullptr) < kMinimumValidTime) {
    Serial.println("ERROR: summary not sent: NTP time is not synchronized.");
    return false;
  }

  size_t payload_length = 0;
  if (!formatPrometheusPayload(summary, prometheus_payload,
                               sizeof(prometheus_payload), payload_length)) {
    Serial.println("ERROR: summary not sent: Prometheus payload is too large.");
    return false;
  }

  NetworkClientSecure client;
  // ponytail: TLS peer verification is intentionally disabled; restore
  // setCACert if token or metric integrity matters.
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, Secrets::kVictoriaMetricsUrl)) {
    Serial.println("ERROR: summary not sent: invalid VictoriaMetrics URL.");
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + Secrets::kBearerToken);
  http.addHeader("Content-Type", "text/plain");
  const int status = http.POST(
      reinterpret_cast<uint8_t*>(prometheus_payload), payload_length);
  http.end();

  if (status < 200 || status >= 300) {
    if (status < 0) {
      Serial.printf("ERROR: VictoriaMetrics POST failed: %s (%d).\n",
                    HTTPClient::errorToString(status).c_str(), status);
    } else {
      Serial.printf("ERROR: VictoriaMetrics POST returned HTTP %d.\n", status);
    }
    return false;
  }
  Serial.printf("VictoriaMetrics POST succeeded: HTTP %d.\n", status);
  return true;
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
  updateErrorLed();
  startNetwork();

  if (!Wire.begin(kI2cSdaPin, kI2cSclPin, kI2cFrequencyHz)) {
    Serial.println("ERROR: failed to initialize I2C bus.");
    sensor_error = true;
    updateErrorLed();
    return;
  }

  if (!selectHm3301I2c()) {
    Serial.println("ERROR: HM3301 did not respond. Check power and wiring.");
    sensor_error = true;
    updateErrorLed();
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

  const uint32_t now = millis();
  maintainNetwork(now);

  if (!sensor_ready) {
    delay(100);
    return;
  }

  if (static_cast<int32_t>(now - next_summary_ms) >= 0) {
    const MinuteSummary summary = finalizeSummary(summary_accumulator);
    printSummary(now, summary);
    if (summary.sample_count > 0) {
      network_error = !sendSummary(summary);
      updateErrorLed();
    }
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
    sensor_error = true;
    updateErrorLed();
    return;
  }
  if (!checksum_valid) {
    Serial.printf(
        "ERROR: HM3301 checksum mismatch after 3 attempts "
        "(calculated=0x%02X, received=0x%02X).\n",
        calculateChecksum(frame), frame[kHm3301FrameSize - 1]);
    printRawFrame(frame);
    sensor_error = true;
    updateErrorLed();
    return;
  }

  sensor_error = false;
  updateErrorLed();
  addMeasurements(summary_accumulator, frame);
}
