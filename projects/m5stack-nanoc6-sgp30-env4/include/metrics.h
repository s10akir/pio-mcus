#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum MetricIndex : size_t {
  kTemperature,
  kHumidity,
  kPressure,
  kTvoc,
  kEco2,
  kMetricCount,
};

struct MetricSummary {
  float average = 0.0f;
  float maximum = 0.0f;
  uint16_t sample_count = 0;
};

struct MinuteSummary {
  MetricSummary metrics[kMetricCount];
};

inline float calculateAbsoluteHumidity(float temperature_celsius,
                                       float relative_humidity_percent) {
  const float humidity =
      fminf(100.0f, fmaxf(0.0f, relative_humidity_percent));
  return 216.7f * (humidity * 0.01f * 6.112f *
                   expf(17.62f * temperature_celsius /
                        (243.12f + temperature_celsius))) /
         (273.15f + temperature_celsius);
}

inline bool formatPrometheusPayload(const MinuteSummary& summary, char* output,
                                    size_t capacity, size_t& length) {
  struct Definition {
    const char* name;
    const char* source;
  };
  constexpr Definition definitions[kMetricCount] = {
      {"temperature_celsius", "env4"},
      {"relative_humidity_percent", "env4"},
      {"pressure_pascals", "env4"},
      {"tvoc_parts_per_billion", "sgp30"},
      {"eco2_parts_per_million", "sgp30"},
  };

  length = 0;
  if (capacity == 0) {
    return false;
  }
  output[0] = '\0';

  for (size_t i = 0; i < kMetricCount; ++i) {
    if (summary.metrics[i].sample_count == 0) {
      continue;
    }
    const int written = snprintf(
        output + length, capacity - length,
        "environment_%s_avg{site=\"home\",location=\"working-room\",device=\"m5stack-nanoc6-02\",source=\"%s\"} %.2f\n"
        "environment_%s_max{site=\"home\",location=\"working-room\",device=\"m5stack-nanoc6-02\",source=\"%s\"} %.2f\n",
        definitions[i].name, definitions[i].source, summary.metrics[i].average,
        definitions[i].name, definitions[i].source, summary.metrics[i].maximum);
    if (written < 0 || static_cast<size_t>(written) >= capacity - length) {
      return false;
    }
    length += static_cast<size_t>(written);
  }
  return true;
}
