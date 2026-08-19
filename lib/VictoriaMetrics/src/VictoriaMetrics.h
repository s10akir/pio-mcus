#pragma once

#include <HTTPClient.h>
#include <NetworkClientSecure.h>

namespace VictoriaMetrics {
inline bool post(const char* url, const char* bearer_token, char* payload,
                 size_t length) {
  NetworkClientSecure client;
  // ponytail: TLS peer verification is intentionally disabled; accept a CA
  // certificate here if token or metric integrity matters.
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("ERROR: VictoriaMetrics POST failed: invalid URL.");
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + bearer_token);
  http.addHeader("Content-Type", "text/plain");
  const int status =
      http.POST(reinterpret_cast<uint8_t*>(payload), length);
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
}  // namespace VictoriaMetrics
