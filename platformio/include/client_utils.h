/* Client utility declarations for esp32-weather-epd.
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

#ifndef __CLIENT_UTILS_H__
#define __CLIENT_UTILS_H__

#include <Arduino.h>
#include <WiFi.h>
#include "api_response.h"

// WiFi connection
wl_status_t startWiFi();
void killWiFi();

// Time synchronization
bool waitForSNTPSync();

// API requests (Open-Meteo)
bool getForecast(om_resp_forecast_t &forecast, String &errorMsg);
bool getAirQuality(om_resp_air_quality_t &airQuality, String &errorMsg);

// Debug
void printHeapUsage();

#endif
