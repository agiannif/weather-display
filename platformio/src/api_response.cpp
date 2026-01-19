/* API response deserialization for esp32-weather-epd.
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

#include "api_response.h"
#include "config.h"
#include <time.h>

// Parse ISO8601 datetime string to Unix timestamp
// Format: "2024-01-15T14:00" or "2024-01-15"
int64_t parseISO8601(const char *datetime)
{
  struct tm tm = {0};
  int year, month, day, hour = 12, minute = 0;

  // Try full datetime format first
  if (sscanf(datetime, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute) >= 3)
  {
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return mktime(&tm);
  }
  return 0;
}

DeserializationError deserializeForecast(const String &json,
                                         om_resp_forecast_t &r)
{
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, json);
  if (error)
  {
    Serial.printf("[debug] Forecast deserialization error: %s\n", error.c_str());
    return error;
  }

#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif

  // Root level
  r.lat = doc["latitude"] | 0.0f;
  r.lon = doc["longitude"] | 0.0f;
  r.timezone = doc["timezone"] | "UTC";
  r.timezone_offset = doc["utc_offset_seconds"] | 0;

  // Current weather
  // Note: Open-Meteo returns temps in Celsius, wind in km/h, pressure in hPa
  // All values stored in their native API units
  JsonObject current = doc["current"];
  r.current.temp = current["temperature_2m"] | 0.0f;
  r.current.feels_like = current["apparent_temperature"] | 0.0f;
  r.current.humidity = current["relative_humidity_2m"] | 0;
  r.current.pressure = static_cast<int>(current["pressure_msl"] | 0.0f);
  r.current.wind_speed = current["wind_speed_10m"] | 0.0f;
  r.current.wind_deg = current["wind_direction_10m"] | 0;
  r.current.wind_gust = current["wind_gusts_10m"] | 0.0f;
  r.current.uvi = current["uv_index"] | 0.0f;
  r.current.visibility = current["visibility"] | 10000;
  r.current.weather_code = current["weather_code"] | 0;
  r.current.is_day = current["is_day"] | 1;

#if DEBUG_LEVEL >= 1
  Serial.printf("[debug] Parsed temp (Celsius): %.2f, humidity: %d, pressure: %d\n",
                r.current.temp, r.current.humidity, r.current.pressure);
#endif

  // Hourly forecast
  JsonObject hourly = doc["hourly"];
  JsonArray hourly_time = hourly["time"];
  JsonArray hourly_temp = hourly["temperature_2m"];
  JsonArray hourly_humidity = hourly["relative_humidity_2m"];
  JsonArray hourly_pop = hourly["precipitation_probability"];
  JsonArray hourly_precip = hourly["precipitation"];
  JsonArray hourly_code = hourly["weather_code"];
  JsonArray hourly_is_day = hourly["is_day"];

  for (int i = 0; i < OM_NUM_HOURLY && i < hourly_time.size(); i++)
  {
    r.hourly[i].dt = parseISO8601(hourly_time[i] | "");
    r.hourly[i].temp = hourly_temp[i] | 0.0f;
    r.hourly[i].humidity = hourly_humidity[i] | 0;
    r.hourly[i].pop = hourly_pop[i] | 0.0f;
    r.hourly[i].precipitation = hourly_precip[i] | 0.0f;
    r.hourly[i].weather_code = hourly_code[i] | 0;
    r.hourly[i].is_day = hourly_is_day[i] | 1;
  }

  // Daily forecast
  JsonObject daily = doc["daily"];
  JsonArray daily_time = daily["time"];
  JsonArray daily_temp_max = daily["temperature_2m_max"];
  JsonArray daily_temp_min = daily["temperature_2m_min"];
  JsonArray daily_sunrise = daily["sunrise"];
  JsonArray daily_sunset = daily["sunset"];
  JsonArray daily_pop = daily["precipitation_probability_max"];
  JsonArray daily_precip = daily["precipitation_sum"];
  JsonArray daily_code = daily["weather_code"];
  JsonArray daily_uvi = daily["uv_index_max"];

  for (int i = 0; i < OM_NUM_DAILY && i < daily_time.size(); i++)
  {
    r.daily[i].dt = parseISO8601(daily_time[i] | "");
    r.daily[i].temp_max = daily_temp_max[i] | 0.0f;
    r.daily[i].temp_min = daily_temp_min[i] | 0.0f;
    r.daily[i].sunrise = parseISO8601(daily_sunrise[i] | "");
    r.daily[i].sunset = parseISO8601(daily_sunset[i] | "");
    r.daily[i].pop = daily_pop[i] | 0.0f;
    r.daily[i].precipitation = daily_precip[i] | 0.0f;
    r.daily[i].weather_code = daily_code[i] | 0;
    r.daily[i].uvi = daily_uvi[i] | 0.0f;
  }

  // Copy sunrise/sunset from daily[0] to current
  r.current.sunrise = r.daily[0].sunrise;
  r.current.sunset = r.daily[0].sunset;

  return DeserializationError::Ok;
}

DeserializationError deserializeAirQuality(const String &json,
                                           om_resp_air_quality_t &r)
{
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, json);
  if (error)
  {
    Serial.printf("[debug] Air quality deserialization error: %s\n", error.c_str());
    return error;
  }

  JsonObject current = doc["current"];
  r.aqi = current["us_aqi"] | 0;

  return DeserializationError::Ok;
}
