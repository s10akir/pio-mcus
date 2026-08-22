#include <Arduino.h>
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedENV.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <time.h>
#include <atomic>

#include <VictoriaMetrics.h>
#include <WifiConnection.h>
#include "metrics.h"
#include "secrets.h"

namespace {
constexpr uint8_t kBlueLedPin = 7;
constexpr uint32_t kSummaryIntervalMs = 60000;
constexpr uint32_t kHumidityUpdateIntervalMs = 60000;
constexpr uint32_t kBaselineSaveIntervalMs = 60UL * 60UL * 1000UL;
constexpr uint32_t kInitialBaselineLearningMs = 12UL * 60UL * 60UL * 1000UL;
constexpr uint64_t kBaselineMaximumAgeSeconds = 7ULL * 24ULL * 60ULL * 60ULL;
constexpr time_t kMinimumValidTime = 1577836800;  // 2020-01-01 UTC
constexpr uint32_t kBaselineMagic = 0x53475031;   // "SGP1"
constexpr size_t kPrometheusPayloadSize = 2048;

struct BaselineRecord {
  uint32_t magic;
  uint16_t co2eq;
  uint16_t tvoc;
  uint64_t sensor_serial;
  uint64_t saved_at;
};

struct MetricAccumulator {
  double sum = 0.0;
  float maximum = 0.0f;
  uint16_t sample_count = 0;
};

constexpr bool isValidBaseline(uint16_t co2eq, uint16_t tvoc) {
  return co2eq != 0xFFFF && tvoc != 0xFFFF;
}

static_assert(isValidBaseline(0x1234, 0x5678));
static_assert(!isValidBaseline(0xFFFF, 0x5678));
static_assert(!isValidBaseline(0x1234, 0xFFFF));

m5::unit::UnitUnified units;
m5::unit::UnitENV4 env4;
m5::unit::UnitSGP30 sgp30;
auto& sht40 = env4.sht40;
auto& bmp280 = env4.bmp280;

MetricAccumulator accumulators[kMetricCount];
QueueHandle_t summary_queue = nullptr;
Preferences preferences;
WifiConnection wifi;
std::atomic_bool sensor_error{true};
bool network_error = true;
bool sensors_ready = false;
bool preferences_ready = false;
bool baseline_restored = false;
uint64_t sensor_serial = 0;
char prometheus_payload[kPrometheusPayloadSize];

void updateErrorLed() {
  digitalWrite(kBlueLedPin,
               sensor_error.load() || network_error ? HIGH : LOW);
}

void startNetwork() {
  wifi.begin(Secrets::kWifiSsid, Secrets::kWifiPassword);
  configTime(0, 0, Secrets::kNtpServer);
}

void maintainNetwork(uint32_t now) {
  if (!wifi.update(now)) {
    network_error = true;
  }
}

void waitForClock() {
  const uint32_t deadline = millis() + 10000;
  while (time(nullptr) < kMinimumValidTime &&
         static_cast<int32_t>(millis() - deadline) < 0) {
    wifi.update(millis());
    delay(100);
  }
}

bool loadBaseline(BaselineRecord& record) {
  if (!preferences_ready ||
      preferences.getBytesLength("baseline") != sizeof(record) ||
      preferences.getBytes("baseline", &record, sizeof(record)) !=
          sizeof(record)) {
    return false;
  }

  if (!isValidBaseline(record.co2eq, record.tvoc)) {
    Serial.printf("Discarding invalid SGP30 baseline: eCO2=0x%04X, "
                  "TVOC=0x%04X\n",
                  record.co2eq, record.tvoc);
    preferences.remove("baseline");
    return false;
  }

  const time_t now = time(nullptr);
  return record.magic == kBaselineMagic &&
         record.sensor_serial == sensor_serial && now >= kMinimumValidTime &&
         record.saved_at <= static_cast<uint64_t>(now) &&
         static_cast<uint64_t>(now) - record.saved_at <=
             kBaselineMaximumAgeSeconds;
}

bool saveBaseline() {
  const time_t now = time(nullptr);
  if (!preferences_ready || now < kMinimumValidTime) {
    Serial.println("ERROR: SGP30 baseline not saved: time is not synchronized.");
    return false;
  }

  BaselineRecord record = {};
  if (!sgp30.readIaqBaseline(record.co2eq, record.tvoc)) {
    Serial.println("ERROR: failed to read SGP30 baseline.");
    return false;
  }
  if (!isValidBaseline(record.co2eq, record.tvoc)) {
    Serial.printf("ERROR: invalid SGP30 baseline not saved: eCO2=0x%04X, "
                  "TVOC=0x%04X\n",
                  record.co2eq, record.tvoc);
    return false;
  }
  record.magic = kBaselineMagic;
  record.sensor_serial = sensor_serial;
  record.saved_at = static_cast<uint64_t>(now);
  if (preferences.putBytes("baseline", &record, sizeof(record)) !=
      sizeof(record)) {
    Serial.println("ERROR: failed to save SGP30 baseline to NVS.");
    return false;
  }

  Serial.printf("SGP30 baseline saved: eCO2=0x%04X, TVOC=0x%04X\n",
                record.co2eq, record.tvoc);
  return true;
}

void addSample(MetricIndex index, float value) {
  if (!isfinite(value)) {
    return;
  }
  MetricAccumulator& target = accumulators[index];
  target.sum += value;
  if (target.sample_count == 0 || value > target.maximum) {
    target.maximum = value;
  }
  ++target.sample_count;
}

MinuteSummary finalizeSummary() {
  MinuteSummary result;
  for (size_t i = 0; i < kMetricCount; ++i) {
    result.metrics[i].sample_count = accumulators[i].sample_count;
    if (accumulators[i].sample_count == 0) {
      continue;
    }
    result.metrics[i].average = static_cast<float>(
        accumulators[i].sum / accumulators[i].sample_count);
    result.metrics[i].maximum = accumulators[i].maximum;
  }
  return result;
}

void sensorTask(void*) {
  uint32_t next_summary_ms = millis() + kSummaryIntervalMs;
  uint32_t next_humidity_update_ms = millis();
  uint32_t next_baseline_save_ms =
      millis() +
      (baseline_restored ? kBaselineSaveIntervalMs
                         : kInitialBaselineLearningMs);
  uint32_t last_sht40_ms = millis();
  uint32_t last_bmp280_ms = millis();
  uint32_t last_sgp30_ms = millis();
  float temperature = NAN;
  float humidity = NAN;

  for (;;) {
    units.update();
    const uint32_t now = millis();

    if (sht40.updated()) {
      temperature = sht40.temperature();
      humidity = sht40.humidity();
      addSample(kTemperature, temperature);
      addSample(kHumidity, humidity);
      last_sht40_ms = now;
    }
    if (bmp280.updated()) {
      addSample(kPressure, bmp280.pressure());
      last_bmp280_ms = now;
    }
    if (sgp30.updated()) {
      addSample(kTvoc, sgp30.tvoc());
      addSample(kEco2, sgp30.co2eq());
      last_sgp30_ms = now;
    }

    if (isfinite(temperature) && isfinite(humidity) &&
        static_cast<int32_t>(now - next_humidity_update_ms) >= 0) {
      const float absolute_humidity =
          calculateAbsoluteHumidity(temperature, humidity);
      if (!sgp30.writeAbsoluteHumidity(absolute_humidity)) {
        Serial.println("ERROR: failed to update SGP30 humidity compensation.");
      }
      next_humidity_update_ms = now + kHumidityUpdateIntervalMs;
    }

    if (static_cast<int32_t>(now - next_baseline_save_ms) >= 0) {
      next_baseline_save_ms =
          now + (saveBaseline() ? kBaselineSaveIntervalMs : 60000);
    }

    if (static_cast<int32_t>(now - next_summary_ms) >= 0) {
      const MinuteSummary summary = finalizeSummary();
      xQueueOverwrite(summary_queue, &summary);
      for (auto& accumulator : accumulators) {
        accumulator = {};
      }
      do {
        next_summary_ms += kSummaryIntervalMs;
      } while (static_cast<int32_t>(now - next_summary_ms) >= 0);
    }

    sensor_error.store(static_cast<uint32_t>(now - last_sht40_ms) > 5000 ||
                       static_cast<uint32_t>(now - last_bmp280_ms) > 5000 ||
                       static_cast<uint32_t>(now - last_sgp30_ms) > 5000);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void printSummary(const MinuteSummary& summary) {
  constexpr const char* labels[kMetricCount] = {
      "temperature (C)", "relative humidity (%)", "pressure (Pa)",
      "TVOC (ppb)",      "eCO2 (ppm)",
  };
  Serial.println("\n1-minute summary");
  for (size_t i = 0; i < kMetricCount; ++i) {
    const MetricSummary& metric = summary.metrics[i];
    if (metric.sample_count == 0) {
      Serial.printf("  %s: no valid samples\n", labels[i]);
    } else {
      Serial.printf("  %s: average=%.2f, max=%.2f, samples=%u\n", labels[i],
                    metric.average, metric.maximum, metric.sample_count);
    }
  }
}

bool sendSummary(const MinuteSummary& summary) {
  if (!wifi.connected()) {
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
  if (payload_length == 0) {
    return true;
  }
  return VictoriaMetrics::post(Secrets::kVictoriaMetricsUrl,
                               Secrets::kBearerToken, prometheus_payload,
                               payload_length);
}
}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);
  Serial.begin(115200);
  delay(100);
  Serial.println("\nM5Stack NanoC6 + SGP30 + ENV.IV");

  pinMode(kBlueLedPin, OUTPUT);
  updateErrorLed();
  startNetwork();
  waitForClock();

  preferences_ready = preferences.begin("sgp30", false);
  if (!preferences_ready) {
    Serial.println("ERROR: failed to open SGP30 baseline storage.");
  }

  auto sgp30_config = sgp30.config();
  sgp30_config.start_periodic = false;
  sgp30.config(sgp30_config);

  M5.Ex_I2C.begin();
  if (!units.add(env4, M5.Ex_I2C) || !units.add(sgp30, M5.Ex_I2C) ||
      !sgp30.readSerialNumber(sensor_serial)) {
    Serial.println("ERROR: failed to find SGP30 or ENV.IV. Check the Unit Hub and cables.");
    return;
  }

  BaselineRecord baseline = {};
  baseline_restored = loadBaseline(baseline);
  if (!units.begin()) {
    Serial.println("ERROR: failed to initialize SGP30 or ENV.IV.");
    return;
  }

  const bool sgp30_started =
      baseline_restored
          ? sgp30.startPeriodicMeasurement(baseline.co2eq, baseline.tvoc, 0)
          : sgp30.startPeriodicMeasurement();
  if (!sgp30_started) {
    Serial.println("ERROR: failed to start SGP30 measurement.");
    return;
  }

  Serial.printf("SGP30 serial: %012llX\n",
                static_cast<unsigned long long>(sensor_serial));
  if (baseline_restored) {
    Serial.printf("SGP30 baseline restored: eCO2=0x%04X, TVOC=0x%04X\n",
                  baseline.co2eq, baseline.tvoc);
  } else {
    Serial.println("No valid SGP30 baseline; learning for 12 hours.");
  }

  summary_queue = xQueueCreate(1, sizeof(MinuteSummary));
  if (!summary_queue ||
      xTaskCreate(sensorTask, "sensors", 6144, nullptr, 2, nullptr) != pdPASS) {
    Serial.println("ERROR: failed to start sensor task.");
    return;
  }
  sensors_ready = true;
}

void loop() {
  M5.update();
  maintainNetwork(millis());

  if (sensors_ready) {
    MinuteSummary summary;
    if (xQueueReceive(summary_queue, &summary, 0) == pdTRUE) {
      printSummary(summary);
      network_error = !sendSummary(summary);
    }
  }

  updateErrorLed();
  delay(10);
}
