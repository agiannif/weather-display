/* Client utilities for esp32-weather-epd.
 * Copyright (C) 2022-2025  Luke Marzen
 * Copyright (C) 2026  Anthony Fenzl
 *
 * Modified to use Open-Meteo API instead of OpenWeatherMap API.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "client_utils.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_sntp.h>

// Open-Meteo API endpoints
static const char *OM_FORECAST_HOST = "api.open-meteo.com";
static const char *OM_AIR_QUALITY_HOST = "air-quality-api.open-meteo.com";

// Helper function to determine if an HTTP error code is retryable
static bool isRetryableError(int httpCode)
{
  // Only retry on transient connection errors, not permanent errors
  switch (httpCode)
  {
    case -1:  // Connection Refused
    case -4:  // Not Connected
    case -5:  // Connection Lost
    case -11: // Read Timeout
      return true;
    default:
      return false;
  }
}

wl_status_t startWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_TIMEOUT)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("WiFi connection failed");
  }

  return WiFi.status();
}

void killWiFi()
{
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

bool waitForSNTPSync()
{
  // Wait for SNTP synchronization to complete
  unsigned long timeout = millis() + NTP_TIMEOUT;
  if ((sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET)
      && (millis() < timeout))
  {
    Serial.print("Waiting for SNTP sync");
    delay(100); // ms
    while ((sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET)
        && (millis() < timeout))
    {
      Serial.print(".");
      delay(100); // ms
    }
    Serial.println();
  }

  // Verify time was actually set using getLocalTime
  tm timeInfo;
  int attempts = 0;
  while (!getLocalTime(&timeInfo) && attempts++ < 3)
  {
    Serial.println("Failed to get local time");
    return false;
  }

  Serial.printf("Time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
                timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
  return true;
}

bool getForecast(om_resp_forecast_t &forecast, String &errorMsg)
{
  WiFiClientSecure client;
  client.setInsecure(); // Open-Meteo uses standard certs, skip verification for simplicity

  HTTPClient http;

  // Build URL with all required parameters
  String url = "https://";
  url += OM_FORECAST_HOST;
  url += "/v1/forecast?latitude=";
  url += LAT;
  url += "&longitude=";
  url += LON;
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,";
  url += "pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m,";
  url += "weather_code,uv_index,visibility,is_day";
  url += "&hourly=temperature_2m,relative_humidity_2m,precipitation_probability,";
  url += "precipitation,weather_code,is_day";
  url += "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset,";
  url += "precipitation_probability_max,precipitation_sum,weather_code,uv_index_max";
  url += "&timezone=";
  url += API_TIMEZONE;
  url += "&forecast_days=8&forecast_hours=48";

  Serial.println("Fetching forecast from Open-Meteo...");
#if DEBUG_LEVEL >= 2
  Serial.println(url);
#endif

  // Retry loop for transient errors
  for (unsigned attempt = 1; attempt <= API_RETRY_ATTEMPTS; attempt++)
  {
    if (attempt > 1)
    {
      Serial.printf("Retry attempt %d/%d\n", attempt, API_RETRY_ATTEMPTS);
    }

    http.begin(client, url);
    http.setTimeout(HTTP_CLIENT_TCP_TIMEOUT);

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
      // Build error message
      String errorType;
      if (httpCode > 0)
      {
        errorType = "HTTP " + String(httpCode);
        Serial.printf("Forecast API error: HTTP %d, RSSI: %d dBm\n", httpCode, WiFi.RSSI());
      }
      else
      {
        switch (httpCode)
        {
          case -1:  errorType = "Connection Refused"; break;
          case -2:  errorType = "Send Header Failed"; break;
          case -3:  errorType = "Send Payload Failed"; break;
          case -4:  errorType = "Not Connected"; break;
          case -5:  errorType = "Connection Lost"; break;
          case -6:  errorType = "No Stream"; break;
          case -7:  errorType = "No HTTP Server"; break;
          case -8:  errorType = "Too Less RAM"; break;
          case -9:  errorType = "Encoding"; break;
          case -10: errorType = "Stream Write"; break;
          case -11: errorType = "Read Timeout"; break;
          default:  errorType = "Unknown Error"; break;
        }
        Serial.printf("Forecast API error: %s (%d), RSSI: %d dBm\n",
                      errorType.c_str(), httpCode, WiFi.RSSI());
      }

      http.end();

      // Check if we should retry
      if (isRetryableError(httpCode) && attempt < API_RETRY_ATTEMPTS)
      {
        Serial.printf("Retryable error, waiting %d ms before retry...\n", API_RETRY_DELAY);
        delay(API_RETRY_DELAY);
        continue; // Try again
      }
      else
      {
        // No more retries or non-retryable error
        errorMsg = errorType + " RSSI:" + String(WiFi.RSSI()) + "dBm";
        if (attempt > 1)
        {
          errorMsg += " (after " + String(attempt) + " attempts)";
        }
        return false;
      }
    }

    // HTTP request succeeded
    String payload = http.getString();
#if DEBUG_LEVEL >= 2
    Serial.printf("HTTP GET successful (code: %d), response length: %d bytes\n",
                  httpCode, payload.length());
#endif
    http.end();

    DeserializationError error = deserializeForecast(payload, forecast);

    if (error)
    {
      Serial.printf("Forecast JSON parsing failed: %s\n", error.c_str());
#if DEBUG_LEVEL >= 2
      Serial.printf("Payload length: %d bytes\n", payload.length());
      if (payload.length() > 0)
      {
        Serial.println("First 200 chars of response:");
        Serial.println(payload.substring(0, min(200, (int)payload.length())));
      }
#endif

      errorMsg = "JSON parse: " + String(error.c_str());
      return false; // Don't retry JSON parsing errors
    }

    Serial.println("Forecast data received successfully");
    return true;
  }

  // Should never reach here, but just in case
  errorMsg = "Unknown error after retries";
  return false;
}

bool getAirQuality(om_resp_air_quality_t &airQuality, String &errorMsg)
{
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url = "https://";
  url += OM_AIR_QUALITY_HOST;
  url += "/v1/air-quality?latitude=";
  url += LAT;
  url += "&longitude=";
  url += LON;
  url += "&current=us_aqi";

  Serial.println("Fetching air quality from Open-Meteo...");
#if DEBUG_LEVEL >= 2
  Serial.println(url);
#endif

  // Retry loop for transient errors
  for (unsigned attempt = 1; attempt <= API_RETRY_ATTEMPTS; attempt++)
  {
    if (attempt > 1)
    {
      Serial.printf("Retry attempt %d/%d\n", attempt, API_RETRY_ATTEMPTS);
    }

    http.begin(client, url);
    http.setTimeout(HTTP_CLIENT_TCP_TIMEOUT);

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
      // Build error message
      String errorType;
      if (httpCode > 0)
      {
        errorType = "HTTP " + String(httpCode);
        Serial.printf("Air Quality API error: HTTP %d, RSSI: %d dBm\n", httpCode, WiFi.RSSI());
      }
      else
      {
        switch (httpCode)
        {
          case -1:  errorType = "Connection Refused"; break;
          case -2:  errorType = "Send Header Failed"; break;
          case -3:  errorType = "Send Payload Failed"; break;
          case -4:  errorType = "Not Connected"; break;
          case -5:  errorType = "Connection Lost"; break;
          case -6:  errorType = "No Stream"; break;
          case -7:  errorType = "No HTTP Server"; break;
          case -8:  errorType = "Too Less RAM"; break;
          case -9:  errorType = "Encoding"; break;
          case -10: errorType = "Stream Write"; break;
          case -11: errorType = "Read Timeout"; break;
          default:  errorType = "Unknown Error"; break;
        }
        Serial.printf("Air Quality API error: %s (%d), RSSI: %d dBm\n",
                      errorType.c_str(), httpCode, WiFi.RSSI());
      }

      http.end();

      // Check if we should retry
      if (isRetryableError(httpCode) && attempt < API_RETRY_ATTEMPTS)
      {
        Serial.printf("Retryable error, waiting %d ms before retry...\n", API_RETRY_DELAY);
        delay(API_RETRY_DELAY);
        continue; // Try again
      }
      else
      {
        // No more retries or non-retryable error
        errorMsg = errorType + " RSSI:" + String(WiFi.RSSI()) + "dBm";
        if (attempt > 1)
        {
          errorMsg += " (after " + String(attempt) + " attempts)";
        }
        return false;
      }
    }

    // HTTP request succeeded
    String payload = http.getString();
#if DEBUG_LEVEL >= 2
    Serial.printf("HTTP GET successful (code: %d), response length: %d bytes\n",
                  httpCode, payload.length());
#endif
    http.end();

    DeserializationError error = deserializeAirQuality(payload, airQuality);

    if (error)
    {
      Serial.printf("Air Quality JSON parsing failed: %s\n", error.c_str());
#if DEBUG_LEVEL >= 2
      Serial.printf("Payload length: %d bytes\n", payload.length());
      if (payload.length() > 0)
      {
        Serial.println("First 200 chars of response:");
        Serial.println(payload.substring(0, min(200, (int)payload.length())));
      }
#endif

      errorMsg = "JSON parse: " + String(error.c_str());
      return false; // Don't retry JSON parsing errors
    }

    Serial.println("Air quality data received successfully");
    return true;
  }

  // Should never reach here, but just in case
  errorMsg = "Unknown error after retries";
  return false;
}

void printHeapUsage()
{
  Serial.println("[debug] Heap Size       : " + String(ESP.getHeapSize()) + " B");
  Serial.println("[debug] Available Heap  : " + String(ESP.getFreeHeap()) + " B");
  Serial.println("[debug] Min Free Heap   : " + String(ESP.getMinFreeHeap()) + " B");
  Serial.println("[debug] Max Allocatable : " + String(ESP.getMaxAllocHeap()) + " B");
}
