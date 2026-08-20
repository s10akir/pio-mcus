#include <assert.h>
#include <math.h>
#include <string.h>

#include "metrics.h"

int main() {
  assert(fabsf(calculateAbsoluteHumidity(25.0f, 50.0f) - 11.48f) < 0.02f);
  assert(fabsf(calculateAbsoluteHumidity(25.0f, 120.0f) -
               calculateAbsoluteHumidity(25.0f, 100.0f)) < 0.001f);

  MinuteSummary summary;
  for (size_t i = 0; i < kMetricCount; ++i) {
    summary.metrics[i].average = static_cast<float>(i) + 1.25f;
    summary.metrics[i].maximum = static_cast<float>(i) + 2.5f;
    summary.metrics[i].sample_count = 60;
  }

  char payload[2048];
  size_t length = 0;
  assert(formatPrometheusPayload(summary, payload, sizeof(payload), length));
  assert(length == strlen(payload));
  size_t lines = 0;
  for (const char* cursor = payload; *cursor; ++cursor) {
    lines += *cursor == '\n';
  }
  assert(lines == 10);
  assert(strstr(payload,
                "environment_pressure_pascals_avg{site=\"home\",location=\"working-room\",device=\"m5stack-nanoc6-02\",source=\"env4\"} 3.25\n") !=
         nullptr);
  assert(strstr(payload,
                "environment_eco2_parts_per_million_max{site=\"home\",location=\"working-room\",device=\"m5stack-nanoc6-02\",source=\"sgp30\"} 6.50\n") !=
         nullptr);

  summary.metrics[kPressure].sample_count = 0;
  assert(formatPrometheusPayload(summary, payload, sizeof(payload), length));
  assert(strstr(payload, "pressure_pascals") == nullptr);

  char too_small[8];
  assert(!formatPrometheusPayload(summary, too_small, sizeof(too_small),
                                  length));
}
