#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

constexpr size_t kPmMeasurementCount = 3;

struct MeasurementSummary {
  float average = 0.0f;
  uint16_t maximum = 0;
};

struct MinuteSummary {
  MeasurementSummary measurements[kPmMeasurementCount];
  uint16_t sensor_number = 0;
  uint8_t sample_count = 0;
};

inline bool formatPrometheusPayload(const MinuteSummary& summary, char* output,
                                    size_t capacity, size_t& length) {
  length = 0;
  if (capacity == 0) {
    return false;
  }

  if (summary.sample_count == 0) {
    output[0] = '\0';
    return true;
  }

  constexpr const char* particle_sizes[] = {"pm1", "pm2_5", "pm10"};
  for (size_t i = 0; i < kPmMeasurementCount; ++i) {
    const int written = snprintf(
        output + length, capacity - length,
        "environment_%s_avg_micrograms_per_cubic_meter{site=\"home\",location=\"working-room\",device=\"m5stack-nanoc6-01\",source=\"hm3301\"} %.1f\n"
        "environment_%s_max_micrograms_per_cubic_meter{site=\"home\",location=\"working-room\",device=\"m5stack-nanoc6-01\",source=\"hm3301\"} %u\n",
        particle_sizes[i], summary.measurements[i].average, particle_sizes[i],
        static_cast<unsigned int>(summary.measurements[i].maximum));
    if (written < 0 || static_cast<size_t>(written) >= capacity - length) {
      return false;
    }
    length += static_cast<size_t>(written);
  }

  return true;
}
