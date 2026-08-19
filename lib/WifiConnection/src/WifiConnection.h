#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WifiConnection {
 public:
  explicit WifiConnection(uint32_t retry_interval_ms = 10000)
      : retry_interval_ms_(retry_interval_ms) {}

  void begin(const char* ssid, const char* password) {
    ssid_ = ssid;
    password_ = password;
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid_, password_);
    next_retry_ms_ = millis() + retry_interval_ms_;
    Serial.printf("Wi-Fi connecting to %s\n", ssid_);
  }

  bool update(uint32_t now) {
    if (connected()) {
      if (!was_connected_) {
        Serial.printf("Wi-Fi connected: %s\n",
                      WiFi.localIP().toString().c_str());
        was_connected_ = true;
      }
      return true;
    }

    if (was_connected_) {
      Serial.println("ERROR: Wi-Fi disconnected.");
      was_connected_ = false;
    }
    if (static_cast<int32_t>(now - next_retry_ms_) >= 0) {
      Serial.println("Wi-Fi reconnecting...");
      WiFi.begin(ssid_, password_);
      next_retry_ms_ = now + retry_interval_ms_;
    }
    return false;
  }

  bool connected() const { return WiFi.status() == WL_CONNECTED; }

 private:
  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  const uint32_t retry_interval_ms_;
  uint32_t next_retry_ms_ = 0;
  bool was_connected_ = false;
};
