/* API response deserialization declarations for esp32-weather-epd.
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

#ifndef __API_RESPONSE_H__
#define __API_RESPONSE_H__

#include <cstdint>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#define OM_NUM_HOURLY  48
#define OM_NUM_DAILY    8

// Current weather data
// Note: All values stored in their native Open-Meteo API units (no conversions)
typedef struct om_current
{
  float   temp;           // temperature_2m (°C)
  float   feels_like;     // apparent_temperature (°C)
  int     humidity;       // relative_humidity_2m (%)
  int     pressure;       // pressure_msl (hPa)
  float   wind_speed;     // wind_speed_10m (km/h)
  int     wind_deg;       // wind_direction_10m (degrees)
  float   wind_gust;      // wind_gusts_10m (km/h)
  float   uvi;            // uv_index
  int     visibility;     // visibility (m)
  int     weather_code;   // WMO weather code
  int     is_day;         // 1 = day, 0 = night
  int64_t sunrise;        // Unix timestamp (from daily[0])
  int64_t sunset;         // Unix timestamp (from daily[0])
} om_current_t;

// Hourly forecast data
// Note: All values in native Open-Meteo API units (temps in °C, wind in km/h)
typedef struct om_hourly
{
  int64_t dt;             // Unix timestamp
  float   temp;           // temperature_2m (°C)
  int     humidity;       // relative_humidity_2m (%)
  float   pop;            // precipitation_probability (0-100)
  float   precipitation;  // precipitation (mm)
  int     weather_code;   // WMO weather code
  int     is_day;         // 1 = day, 0 = night
} om_hourly_t;

// Daily forecast data
// Note: All values in native Open-Meteo API units (temps in °C)
typedef struct om_daily
{
  int64_t dt;             // Unix timestamp (noon of the day)
  float   temp_min;       // temperature_2m_min (°C)
  float   temp_max;       // temperature_2m_max (°C)
  int64_t sunrise;        // Unix timestamp
  int64_t sunset;         // Unix timestamp
  float   pop;            // precipitation_probability_max (0-100)
  float   precipitation;  // precipitation_sum (mm)
  int     weather_code;   // WMO weather code
  float   uvi;            // uv_index_max
} om_daily_t;

// Combined forecast response
typedef struct om_resp_forecast
{
  float   lat;
  float   lon;
  String  timezone;
  int     timezone_offset;  // utc_offset_seconds
  om_current_t  current;
  om_hourly_t   hourly[OM_NUM_HOURLY];
  om_daily_t    daily[OM_NUM_DAILY];
} om_resp_forecast_t;

// Air quality response
typedef struct om_resp_air_quality
{
  int     aqi;            // US AQI value (0-500)
} om_resp_air_quality_t;

// Deserialization functions
DeserializationError deserializeForecast(const String &json,
                                         om_resp_forecast_t &r);
DeserializationError deserializeAirQuality(const String &json,
                                           om_resp_air_quality_t &r);

// Helper to parse ISO8601 datetime to Unix timestamp
int64_t parseISO8601(const char *datetime);

#endif
