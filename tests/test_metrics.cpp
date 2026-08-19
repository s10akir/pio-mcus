#include <assert.h>
#include <string.h>

#include "metrics.h"

int main() {
  MinuteSummary summary;
  summary.sensor_number = 42;
  summary.sample_count = 60;
  for (size_t i = 0; i < kPmMeasurementCount; ++i) {
    summary.measurements[i].average = static_cast<float>(i) + 1.5f;
    summary.measurements[i].maximum = static_cast<uint16_t>(i + 10);
  }

  char payload[1536];
  size_t length = 0;
  assert(formatPrometheusPayload(summary, payload, sizeof(payload), length));
  assert(length == strlen(payload));
  size_t lines = 0;
  for (const char* cursor = payload; *cursor; ++cursor) {
    lines += *cursor == '\n';
  }
  assert(lines == 6);
  assert(strstr(payload,
                "environment_pm2_5_avg_micrograms_per_cubic_meter{site=\"home\",location=\"working-room\",device=\"m5stack-nanoc6-01\",source=\"hm3301\"} 2.5\n") !=
         nullptr);
  assert(strstr(payload, "valid_samples") == nullptr);

  summary = {};
  assert(formatPrometheusPayload(summary, payload, sizeof(payload), length));
  assert(length == 0);
  assert(strcmp(payload, "") == 0);

  summary.sample_count = 1;
  char too_small[8];
  assert(!formatPrometheusPayload(summary, too_small, sizeof(too_small),
                                  length));
}
