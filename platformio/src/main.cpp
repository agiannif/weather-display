/* Main program for esp32-weather-epd.
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

#include "config.h"
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>
#include <time.h>
#include <WiFi.h>
#include <Wire.h>

#include "_locale.h"
#include "api_response.h"
#include "client_utils.h"
#include "config.h"
#include "display_utils.h"
#include "icons/icons_196x196.h"
#include "renderer.h"

#if defined(SENSOR_BME280)
  #include <Adafruit_BME280.h>
#endif
#if defined(SENSOR_BME680)
  #include <Adafruit_BME680.h>
#endif
#include <WiFiClientSecure.h>

// too large to allocate locally on stack
static om_resp_forecast_t    forecast;
static om_resp_air_quality_t air_quality;

#if STALE_DATA_ON_API_FAIL
// RTC memory: survives deep sleep, lost on power-off/reset. ~2KB total.
RTC_DATA_ATTR static bool                      rtc_cache_valid = false;
RTC_DATA_ATTR static om_resp_forecast_cached_t rtc_forecast;
RTC_DATA_ATTR static int                       rtc_aqi;
RTC_DATA_ATTR static char                      rtc_refreshTimeStr[48];
#endif

Preferences prefs;

/* Put esp32 into ultra low-power deep sleep (<11μA).
 * Aligns wake time to the minute. Sleep times defined in config.cpp.
 */
void beginDeepSleep(unsigned long startTime, tm *timeInfo)
{
  if (!getLocalTime(timeInfo))
  {
    Serial.println(TXT_REFERENCING_OLDER_TIME_NOTICE);
  }

  // To simplify sleep time calculations, the current time stored by timeInfo
  // will be converted to time relative to the WAKE_TIME. This way if a
  // SLEEP_DURATION is not a multiple of 60 minutes it can be more trivially,
  // aligned and it can easily be deterimined whether we must sleep for
  // additional time due to bedtime.
  // i.e. when curHour == 0, then timeInfo->tm_hour == WAKE_TIME
  int bedtimeHour = INT_MAX;
  if (BED_TIME != WAKE_TIME)
  {
    bedtimeHour = (BED_TIME - WAKE_TIME + 24) % 24;
  }

  // time is relative to wake time
  int curHour = (timeInfo->tm_hour - WAKE_TIME + 24) % 24;
  const int curMinute = curHour * 60 + timeInfo->tm_min;
  const int curSecond = curHour * 3600
                      + timeInfo->tm_min * 60
                      + timeInfo->tm_sec;
  const int desiredSleepSeconds = SLEEP_DURATION * 60;
  const int offsetMinutes = curMinute % SLEEP_DURATION;
  const int offsetSeconds = curSecond % desiredSleepSeconds;

  // align wake time to nearest multiple of SLEEP_DURATION
  int sleepMinutes = SLEEP_DURATION - offsetMinutes;
  if (desiredSleepSeconds - offsetSeconds < 120
   || offsetSeconds / (float)desiredSleepSeconds > 0.95f)
  { // if we have a sleep time less than 2 minutes OR less 5% SLEEP_DURATION,
    // skip to next alignment
    sleepMinutes += SLEEP_DURATION;
  }

  // estimated wake time, if this falls in a sleep period then sleepDuration
  // must be adjusted
  const int predictedWakeHour = ((curMinute + sleepMinutes) / 60) % 24;

  uint64_t sleepDuration;
  if (predictedWakeHour < bedtimeHour)
  {
    sleepDuration = sleepMinutes * 60 - timeInfo->tm_sec;
  }
  else
  {
    const int hoursUntilWake = 24 - curHour;
    sleepDuration = hoursUntilWake * 3600ULL
                    - (timeInfo->tm_min * 60ULL + timeInfo->tm_sec);
  }

  // add extra delay to compensate for esp32's with fast RTCs.
  sleepDuration += 3ULL;
  sleepDuration *= 1.0015f;

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  esp_sleep_enable_timer_wakeup(sleepDuration * 1000000ULL);
  Serial.print(TXT_AWAKE_FOR);
  Serial.println(" "  + String((millis() - startTime) / 1000.0, 3) + "s");
  Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
  Serial.println(" " + String(sleepDuration) + "s");
  esp_deep_sleep_start();
} // end beginDeepSleep

/* Program entry point.
 */
void setup()
{
  unsigned long startTime = millis();
  Serial.begin(115200);

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  disableBuiltinLED();

  // Open namespace for read/write to non-volatile storage
  prefs.begin(NVS_NAMESPACE, false);

#if BATTERY_MONITORING
  uint32_t batteryVoltage = readBatteryVoltage();
  Serial.print(TXT_BATTERY_VOLTAGE);
  Serial.println(": " + String(batteryVoltage) + "mv");

  // When the battery is low, the display should be updated to reflect that, but
  // only the first time we detect low voltage. The next time the display will
  // refresh is when voltage is no longer low. To keep track of that we will
  // make use of non-volatile storage.
  bool lowBat = prefs.getBool("lowBat", false);

  // low battery, deep sleep now
  if (batteryVoltage <= LOW_BATTERY_VOLTAGE)
  {
    if (lowBat == false)
    { // battery is now low for the first time
      prefs.putBool("lowBat", true);
      prefs.end();
      initDisplay();
      do
      {
        drawError(battery_alert_0deg_196x196, TXT_LOW_BATTERY);
      } while (display.nextPage());
      powerOffDisplay();
    }

    if (batteryVoltage <= CRIT_LOW_BATTERY_VOLTAGE)
    { // critically low battery
      // don't set esp_sleep_enable_timer_wakeup();
      // We won't wake up again until someone manually presses the RST button.
      Serial.println(TXT_CRIT_LOW_BATTERY_VOLTAGE);
      Serial.println(TXT_HIBERNATING_INDEFINITELY_NOTICE);
    }
    else if (batteryVoltage <= VERY_LOW_BATTERY_VOLTAGE)
    { // very low battery
      esp_sleep_enable_timer_wakeup(VERY_LOW_BATTERY_SLEEP_INTERVAL
                                    * 60ULL * 1000000ULL);
      Serial.println(TXT_VERY_LOW_BATTERY_VOLTAGE);
      Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
      Serial.println(" " + String(VERY_LOW_BATTERY_SLEEP_INTERVAL) + "min");
    }
    else
    { // low battery
      esp_sleep_enable_timer_wakeup(LOW_BATTERY_SLEEP_INTERVAL
                                    * 60ULL * 1000000ULL);
      Serial.println(TXT_LOW_BATTERY_VOLTAGE);
      Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
      Serial.println(" " + String(LOW_BATTERY_SLEEP_INTERVAL) + "min");
    }
    esp_deep_sleep_start();
  }
  // battery is no longer low, reset variable in non-volatile storage
  if (lowBat == true)
  {
    prefs.putBool("lowBat", false);
  }
#else
  uint32_t batteryVoltage = UINT32_MAX;
#endif

  // All data should have been loaded from NVS. Close filesystem.
  prefs.end();

  String statusStr = {};
  String tmpStr = {};
#if STALE_DATA_ON_API_FAIL
  bool usingCachedData = false;
#endif
  tm timeInfo = {};

  // START WIFI
  wl_status_t wifiStatus = startWiFi();
  int wifiRSSI = (wifiStatus == WL_CONNECTED) ? WiFi.RSSI() : 0;
  if (wifiStatus != WL_CONNECTED)
  { // WiFi Connection Failed
    killWiFi();
    initDisplay();
    if (wifiStatus == WL_NO_SSID_AVAIL)
    {
      Serial.println(TXT_NETWORK_NOT_AVAILABLE);
      do
      {
        drawError(wifi_x_196x196, TXT_NETWORK_NOT_AVAILABLE);
      } while (display.nextPage());
    }
    else
    {
      Serial.println(TXT_WIFI_CONNECTION_FAILED);
      do
      {
        drawError(wifi_x_196x196, TXT_WIFI_CONNECTION_FAILED);
      } while (display.nextPage());
    }
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }

  // TIME SYNCHRONIZATION
  configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
  bool timeConfigured = waitForSNTPSync();
  if (!timeConfigured)
  {
    Serial.println(TXT_TIME_SYNCHRONIZATION_FAILED);
    killWiFi();
    initDisplay();
    do
    {
      drawError(wi_time_4_196x196, TXT_TIME_SYNCHRONIZATION_FAILED);
    } while (display.nextPage());
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }
  // Get current time info
  time_t now = time(nullptr);
  localtime_r(&now, &timeInfo);

  // MAKE API REQUESTS (Open-Meteo)
  String forecastError;
  bool forecastOk = getForecast(forecast, forecastError);
#if STALE_DATA_ON_API_FAIL
  if (!forecastOk && rtc_cache_valid)
  {
    forecast.lat             = rtc_forecast.lat;
    forecast.lon             = rtc_forecast.lon;
    forecast.timezone_offset = rtc_forecast.timezone_offset;
    forecast.current         = rtc_forecast.current;
    memcpy(forecast.hourly, rtc_forecast.hourly, sizeof(rtc_forecast.hourly));
    memcpy(forecast.daily,  rtc_forecast.daily,  sizeof(rtc_forecast.daily));
    forecastOk      = true;
    usingCachedData = true;
    statusStr       = forecastError;
  }
#endif
  if (!forecastOk)
  {
    killWiFi();
    tmpStr = forecastError;
    initDisplay();
    do
    {
      drawError(wi_cloud_down_196x196, "Forecast API Error", tmpStr);
    } while (display.nextPage());
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }
  String airQualityError;
  bool airQualityOk = getAirQuality(air_quality, airQualityError);
#if STALE_DATA_ON_API_FAIL
  if (!airQualityOk && rtc_cache_valid)
  {
    air_quality.aqi = rtc_aqi;
    airQualityOk    = true;
    if (statusStr.isEmpty())
      statusStr = airQualityError;
    else
      statusStr += " | " + airQualityError;
    usingCachedData = true;
  }
#endif
  if (!airQualityOk)
  {
    killWiFi();
    tmpStr = airQualityError;
    initDisplay();
    do
    {
      drawError(wi_cloud_down_196x196, "Air Quality API Error", tmpStr);
    } while (display.nextPage());
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }
  killWiFi(); // WiFi no longer needed

  // GET INDOOR TEMPERATURE AND HUMIDITY, start BMEx80...
  pinMode(PIN_BME_PWR, OUTPUT);
  digitalWrite(PIN_BME_PWR, HIGH);
#if defined(SENSOR_INIT_DELAY_MS) && SENSOR_INIT_DELAY_MS > 0
  delay(SENSOR_INIT_DELAY_MS);
#endif
  TwoWire I2C_bme = TwoWire(0);
  I2C_bme.begin(PIN_BME_SDA, PIN_BME_SCL, 100000); // 100kHz
  float inTemp     = NAN;
  float inHumidity = NAN;
#if defined(SENSOR_BME280)
  Serial.print(String(TXT_READING_FROM) + " BME280... ");
  Adafruit_BME280 bme;

  if(bme.begin(BME_ADDRESS, &I2C_bme))
  {
#endif
#if defined(SENSOR_BME680)
  Serial.print(String(TXT_READING_FROM) + " BME680... ");
  Adafruit_BME680 bme(&I2C_bme);

  if(bme.begin(BME_ADDRESS))
  {
#endif
    inTemp     = bme.readTemperature(); // Celsius
    inHumidity = bme.readHumidity();    // %

    // check if BME readings are valid
    // note: readings are checked again before drawing to screen. If a reading
    //       is not a number (NAN) then an error occurred, a dash '-' will be
    //       displayed.
    if (std::isnan(inTemp) || std::isnan(inHumidity))
    {
      if (statusStr.isEmpty()) statusStr = "BME " + String(TXT_READ_FAILED);
      else                     statusStr += " | BME " + String(TXT_READ_FAILED);
      Serial.println(statusStr);
    }
    else
    {
      Serial.println(TXT_SUCCESS);
    }
  }
  else
  {
    if (statusStr.isEmpty()) statusStr = "BME " + String(TXT_NOT_FOUND);
    else                     statusStr += " | BME " + String(TXT_NOT_FOUND);
    Serial.println(statusStr);
  }
  digitalWrite(PIN_BME_PWR, LOW);

  String refreshTimeStr;
  getRefreshTimeStr(refreshTimeStr, timeConfigured, &timeInfo);
#if STALE_DATA_ON_API_FAIL
  if (!usingCachedData)
  {
    // Save fresh data to RTC cache for future fallback
    rtc_forecast.lat             = forecast.lat;
    rtc_forecast.lon             = forecast.lon;
    rtc_forecast.timezone_offset = forecast.timezone_offset;
    rtc_forecast.current         = forecast.current;
    memcpy(rtc_forecast.hourly, forecast.hourly, sizeof(forecast.hourly));
    memcpy(rtc_forecast.daily,  forecast.daily,  sizeof(forecast.daily));
    rtc_aqi = air_quality.aqi;
    refreshTimeStr.toCharArray(rtc_refreshTimeStr, sizeof(rtc_refreshTimeStr));
    rtc_cache_valid = true;
  }
  else
  {
    // Restore last-successful-refresh time so display shows when data was valid
    refreshTimeStr = String(rtc_refreshTimeStr);
  }
#endif
  String dateStr;
  getDateStr(dateStr, &timeInfo);

  // RENDER FULL REFRESH
  initDisplay();
  do
  {
    drawCurrentConditions(forecast.current, forecast.daily[0],
                          air_quality, inTemp, inHumidity);
    drawOutlookGraph(forecast.hourly, forecast.daily, timeInfo);
    drawForecast(forecast.daily, timeInfo);
    drawLocationDate(CITY_STRING, dateStr);
    drawStatusBar(statusStr, refreshTimeStr, wifiRSSI, batteryVoltage);
  } while (display.nextPage());
  powerOffDisplay();

  // DEEP SLEEP
  beginDeepSleep(startTime, &timeInfo);
} // end setup

/* This will never run
 */
void loop()
{
} // end loop

