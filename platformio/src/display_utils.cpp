/* Display helper utilities for esp32-weather-epd.
 * Copyright (C) 2022-2025  Luke Marzen
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

#include <cmath>
#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include "_locale.h"
#include "_strftime.h"
#include "api_response.h"
#include "config.h"
#include "display_utils.h"

// icon header files
#include "icons/icons.h"

// Forward declaration of template function
template <int BitmapSize>
const uint8_t *getIconFromWMO(int code, bool is_day);

/* Returns battery voltage in millivolts (mv).
 */
uint32_t readBatteryVoltage()
{
  esp_adc_cal_characteristics_t adc_chars;
  // __attribute__((unused)) disables compiler warnings about this variable
  // being unused (Clang, GCC) which is the case when DEBUG_LEVEL == 0.
  esp_adc_cal_value_t val_type __attribute__((unused));
  adc_power_acquire();
  uint16_t adc_val = analogRead(PIN_BAT_ADC);
  adc_power_release();

  // We will use the eFuse ADC calibration bits, to get accurate voltage
  // readings. The DFRobot FireBeetle Esp32-E V1.0's ADC is 12 bit, and uses
  // 11db attenuation, which gives it a measurable input voltage range of 150mV
  // to 2450mV.
  val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_11db,
                                      ADC_WIDTH_BIT_12, 1100, &adc_chars);

#if DEBUG_LEVEL >= 1
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
  {
    Serial.println("[debug] ADC Cal eFuse Vref");
  }
  else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
  {
    Serial.println("[debug] ADC Cal Two Point");
  }
  else
  {
    Serial.println("[debug] ADC Cal Default");
  }
#endif

  uint32_t batteryVoltage = esp_adc_cal_raw_to_voltage(adc_val, &adc_chars);
  // DFRobot FireBeetle Esp32-E V1.0 voltage divider (1M+1M), so readings are
  // multiplied by 2.
  batteryVoltage *= 2;
  return batteryVoltage;
} // end readBatteryVoltage

/* Returns battery percentage, rounded to the nearest integer.
 * Takes a voltage in millivolts and uses a sigmoidal approximation to find an
 * approximation of the battery life percentage remaining.
 *
 * This function contains LGPLv3 code from
 * <https://github.com/rlogiacco/BatterySense>.
 *
 * Symmetric sigmoidal approximation
 * <https://www.desmos.com/calculator/7m9lu26vpy>
 *
 * c - c / (1 + k*x/v)^3
 */
uint32_t calcBatPercent(uint32_t v, uint32_t minv, uint32_t maxv)
{
  // slow
  //uint32_t p = 110 - (110 / (1 + pow(1.468 * (v - minv)/(maxv - minv), 6)));

  // steep
  //uint32_t p = 102 - (102 / (1 + pow(1.621 * (v - minv)/(maxv - minv), 8.1)));

  // normal
  uint32_t p = 105 - (105 / (1 + pow(1.724 * (v - minv)/(maxv - minv), 5.5)));
  return p >= 100 ? 100 : p;
} // end calcBatPercent

/* Returns 24x24 bitmap incidcating battery status.
 */
const uint8_t *getBatBitmap24(uint32_t batPercent)
{
  if (batPercent >= 93)
  {
    return battery_full_90deg_24x24;
  }
  else if (batPercent >= 79)
  {
    return battery_6_bar_90deg_24x24;
  }
  else if (batPercent >= 65)
  {
    return battery_5_bar_90deg_24x24;
  }
  else if (batPercent >= 50)
  {
    return battery_4_bar_90deg_24x24;
  }
  else if (batPercent >= 36)
  {
    return battery_3_bar_90deg_24x24;
  }
  else if (batPercent >= 22)
  {
    return battery_2_bar_90deg_24x24;
  }
  else if (batPercent >= 8)
  {
    return battery_1_bar_90deg_24x24;
  }
  else
  {  // batPercent < 8
    return battery_0_bar_90deg_24x24;
  }
} // end getBatBitmap24

/* Gets string with the current date.
 */
void getDateStr(String &s, tm *timeInfo)
{
  char buf[48] = {};
  _strftime(buf, sizeof(buf), DATE_FORMAT, timeInfo);
  s = buf;

  // remove double spaces. %e will add an extra space, ie. " 1" instead of "1"
  s.replace("  ", " ");
  return;
} // end getDateStr

/* Gets string with the current date and time of the current refresh attempt.
 */
void getRefreshTimeStr(String &s, bool timeSuccess, tm *timeInfo)
{
  if (timeSuccess == false)
  {
    s = TXT_UNKNOWN;
    return;
  }

  char buf[48] = {};
  _strftime(buf, sizeof(buf), REFRESH_TIME_FORMAT, timeInfo);
  s = buf;

  // remove double spaces.
  s.replace("  ", " ");
  return;
} // end getRefreshTimeStr

/* Takes a String and capitalizes the first letter of every word.
 *
 * Ex:
 *   input   : "severe thunderstorm warning" or "SEVERE THUNDERSTORM WARNING"
 *   becomes : "Severe Thunderstorm Warning"
 */
void toTitleCase(String &text)
{
  text.setCharAt(0, toUpperCase(text.charAt(0)));

  for (int i = 1; i < text.length(); ++i)
  {
    if (text.charAt(i - 1) == ' '
     || text.charAt(i - 1) == '-'
     || text.charAt(i - 1) == '(')
    {
      text.setCharAt(i, toUpperCase(text.charAt(i)));
    }
    else
    {
      text.setCharAt(i, toLowerCase(text.charAt(i)));
    }
  }

  return;
} // end toTitleCase

/* Returns the descriptor text for the given UV index.
 */
const char *getUVIdesc(unsigned int uvi)
{
  if (uvi <= 2)
  {
    return TXT_UV_LOW;
  }
  else if (uvi <= 5)
  {
    return TXT_UV_MODERATE;
  }
  else if (uvi <= 7)
  {
    return TXT_UV_HIGH;
  }
  else if (uvi <= 10)
  {
    return TXT_UV_VERY_HIGH;
  }
  else // uvi >= 11
  {
    return TXT_UV_EXTREME;
  }
} // end getUVIdesc

/* Returns the descriptor text for the given AQI value (US EPA scale).
 * AQI ranges: 0-50 Good, 51-100 Moderate, 101-150 Unhealthy for Sensitive Groups,
 * 151-200 Unhealthy, 201-300 Very Unhealthy, 301+ Hazardous
 */
const char *getAQIdesc(int aqi)
{
  if (aqi <= 50)
  {
    return UNITED_STATES_AQI_TXT[0]; // Good
  }
  else if (aqi <= 100)
  {
    return UNITED_STATES_AQI_TXT[1]; // Moderate
  }
  else if (aqi <= 150)
  {
    return UNITED_STATES_AQI_TXT[2]; // Unhealthy for Sensitive Groups
  }
  else if (aqi <= 200)
  {
    return UNITED_STATES_AQI_TXT[3]; // Unhealthy
  }
  else if (aqi <= 300)
  {
    return UNITED_STATES_AQI_TXT[4]; // Very Unhealthy
  }
  else // aqi > 300
  {
    return UNITED_STATES_AQI_TXT[5]; // Hazardous
  }
} // end getAQIdesc

/* Returns the wifi signal strength descriptor text for the given RSSI.
 */
const char *getWiFidesc(int rssi)
{
  if (rssi == 0)
  {
    return TXT_WIFI_NO_CONNECTION;
  }
  else if (rssi >= -50)
  {
    return TXT_WIFI_EXCELLENT;
  }
  else if (rssi >= -60)
  {
    return TXT_WIFI_GOOD;
  }
  else if (rssi >= -70)
  {
    return TXT_WIFI_FAIR;
  }
  else
  {  // rssi < -70
    return TXT_WIFI_WEAK;
  }
} // end getWiFidesc

/* Returns 16x16 bitmap incidcating wifi status.
 */
const uint8_t *getWiFiBitmap16(int rssi)
{
  if (rssi == 0)
  {
    return wifi_x_16x16;
  }
  else if (rssi >= -50)
  {
    return wifi_16x16;
  }
  else if (rssi >= -60)
  {
    return wifi_3_bar_16x16;
  }
  else if (rssi >= -70)
  {
    return wifi_2_bar_16x16;
  }
  else
  {  // rssi < -70
    return wifi_1_bar_16x16;
  }
} // end getWiFiBitmap24

/* Takes the hourly weather forecast and returns a pointer to the icon's 32x32
 * bitmap using WMO weather codes.
 */
const uint8_t *getHourlyForecastBitmap32(const om_hourly_t &hourly,
                                         const om_daily_t  &today)
{
  (void)today; // unused, kept for API compatibility
  return getIconFromWMO<32>(hourly.weather_code, hourly.is_day);
}

/* Takes the daily weather forecast and returns a pointer to the icon's 64x64
 * bitmap using WMO weather codes.
 */
const uint8_t *getDailyForecastBitmap64(const om_daily_t &daily)
{
  // always show daytime icon for daily forecast
  return getIconFromWMO<64>(daily.weather_code, true);
} // end getDailyForecastBitmap64

/* Takes the current weather and returns a pointer to the icon's 196x196
 * bitmap using WMO weather codes.
 */
const uint8_t *getCurrentConditionsBitmap196(const om_current_t &current,
                                             const om_daily_t   &today)
{
  (void)today; // unused, kept for API compatibility
  return getIconFromWMO<196>(current.weather_code, current.is_day);
} // end getCurrentConditionsBitmap196

#ifdef WIND_ICONS_CARDINAL
static const unsigned char *wind_direction_icon_arr[] = {
  wind_direction_meteorological_0deg_24x24,    // N
  wind_direction_meteorological_90deg_24x24,   // E
  wind_direction_meteorological_180deg_24x24,  // S
  wind_direction_meteorological_270deg_24x24}; // W
#endif // end WIND_ICONS_CARDINAL
#ifdef WIND_ICONS_INTERCARDINAL
static const unsigned char *wind_direction_icon_arr[] = {
  wind_direction_meteorological_0deg_24x24,    // N
  wind_direction_meteorological_45deg_24x24,   // NE
  wind_direction_meteorological_90deg_24x24,   // E
  wind_direction_meteorological_135deg_24x24,  // SE
  wind_direction_meteorological_180deg_24x24,  // S
  wind_direction_meteorological_225deg_24x24,  // SW
  wind_direction_meteorological_270deg_24x24,  // W
  wind_direction_meteorological_315deg_24x24}; // NW
#endif // end WIND_ICONS_INTERCARDINAL
#ifdef WIND_ICONS_SECONDARY_INTERCARDINAL
static const unsigned char *wind_direction_icon_arr[] = {
  wind_direction_meteorological_0deg_24x24,      // N
  wind_direction_meteorological_22_5deg_24x24,   // NNE
  wind_direction_meteorological_45deg_24x24,     // NE
  wind_direction_meteorological_67_5deg_24x24,   // ENE
  wind_direction_meteorological_90deg_24x24,     // E
  wind_direction_meteorological_112_5deg_24x24,  // ESE
  wind_direction_meteorological_135deg_24x24,    // SE
  wind_direction_meteorological_157_5deg_24x24,  // SSE
  wind_direction_meteorological_180deg_24x24,    // S
  wind_direction_meteorological_202_5deg_24x24,  // SSW
  wind_direction_meteorological_225deg_24x24,    // SW
  wind_direction_meteorological_247_5deg_24x24,  // WSW
  wind_direction_meteorological_270deg_24x24,    // W
  wind_direction_meteorological_292_5deg_24x24,  // WNW
  wind_direction_meteorological_315deg_24x24,    // NW
  wind_direction_meteorological_337_5deg_24x24}; // NNW
#endif // end WIND_ICONS_SECONDARY_INTERCARDINAL
#ifdef WIND_ICONS_TERTIARY_INTERCARDINAL
static const unsigned char *wind_direction_icon_arr[] = {
  wind_direction_meteorological_0deg_24x24,       // N
  wind_direction_meteorological_11_25deg_24x24,   // NbE
  wind_direction_meteorological_22_5deg_24x24,    // NNE
  wind_direction_meteorological_33_75deg_24x24,   // NEbN
  wind_direction_meteorological_45deg_24x24,      // NE
  wind_direction_meteorological_56_25deg_24x24,   // NEbE
  wind_direction_meteorological_67_5deg_24x24,    // ENE
  wind_direction_meteorological_78_75deg_24x24,   // EbN
  wind_direction_meteorological_90deg_24x24,      // E
  wind_direction_meteorological_101_25deg_24x24,  // EbS
  wind_direction_meteorological_112_5deg_24x24,   // ESE
  wind_direction_meteorological_123_75deg_24x24,  // SEbE
  wind_direction_meteorological_135deg_24x24,     // SE
  wind_direction_meteorological_146_25deg_24x24,  // SEbS
  wind_direction_meteorological_157_5deg_24x24,   // SSE
  wind_direction_meteorological_168_75deg_24x24,  // SbE
  wind_direction_meteorological_180deg_24x24,     // S
  wind_direction_meteorological_191_25deg_24x24,  // SbW
  wind_direction_meteorological_202_5deg_24x24,   // SSW
  wind_direction_meteorological_213_75deg_24x24,  // SWbS
  wind_direction_meteorological_225deg_24x24,     // SW
  wind_direction_meteorological_236_25deg_24x24,  // SWbW
  wind_direction_meteorological_247_5deg_24x24,   // WSW
  wind_direction_meteorological_258_75deg_24x24,  // WbS
  wind_direction_meteorological_270deg_24x24,     // W
  wind_direction_meteorological_281_25deg_24x24,  // WbN
  wind_direction_meteorological_292_5deg_24x24,   // WNW
  wind_direction_meteorological_303_75deg_24x24,  // NWbW
  wind_direction_meteorological_315deg_24x24,     // NW
  wind_direction_meteorological_326_25deg_24x24,  // NWbN
  wind_direction_meteorological_337_5deg_24x24,   // NNW
  wind_direction_meteorological_348_75deg_24x24}; // NbW
#endif // end WIND_ICONS_TERTIARY_INTERCARDINAL
#ifdef WIND_ICONS_360
static const unsigned char *wind_direction_icon_arr[] = {
  wind_direction_meteorological_0deg_24x24,
  wind_direction_meteorological_1deg_24x24,
  wind_direction_meteorological_2deg_24x24,
  wind_direction_meteorological_3deg_24x24,
  wind_direction_meteorological_4deg_24x24,
  wind_direction_meteorological_5deg_24x24,
  wind_direction_meteorological_6deg_24x24,
  wind_direction_meteorological_7deg_24x24,
  wind_direction_meteorological_8deg_24x24,
  wind_direction_meteorological_9deg_24x24,
  wind_direction_meteorological_10deg_24x24,
  wind_direction_meteorological_11deg_24x24,
  wind_direction_meteorological_12deg_24x24,
  wind_direction_meteorological_13deg_24x24,
  wind_direction_meteorological_14deg_24x24,
  wind_direction_meteorological_15deg_24x24,
  wind_direction_meteorological_16deg_24x24,
  wind_direction_meteorological_17deg_24x24,
  wind_direction_meteorological_18deg_24x24,
  wind_direction_meteorological_19deg_24x24,
  wind_direction_meteorological_20deg_24x24,
  wind_direction_meteorological_21deg_24x24,
  wind_direction_meteorological_22deg_24x24,
  wind_direction_meteorological_23deg_24x24,
  wind_direction_meteorological_24deg_24x24,
  wind_direction_meteorological_25deg_24x24,
  wind_direction_meteorological_26deg_24x24,
  wind_direction_meteorological_27deg_24x24,
  wind_direction_meteorological_28deg_24x24,
  wind_direction_meteorological_29deg_24x24,
  wind_direction_meteorological_30deg_24x24,
  wind_direction_meteorological_31deg_24x24,
  wind_direction_meteorological_32deg_24x24,
  wind_direction_meteorological_33deg_24x24,
  wind_direction_meteorological_34deg_24x24,
  wind_direction_meteorological_35deg_24x24,
  wind_direction_meteorological_36deg_24x24,
  wind_direction_meteorological_37deg_24x24,
  wind_direction_meteorological_38deg_24x24,
  wind_direction_meteorological_39deg_24x24,
  wind_direction_meteorological_40deg_24x24,
  wind_direction_meteorological_41deg_24x24,
  wind_direction_meteorological_42deg_24x24,
  wind_direction_meteorological_43deg_24x24,
  wind_direction_meteorological_44deg_24x24,
  wind_direction_meteorological_45deg_24x24,
  wind_direction_meteorological_46deg_24x24,
  wind_direction_meteorological_47deg_24x24,
  wind_direction_meteorological_48deg_24x24,
  wind_direction_meteorological_49deg_24x24,
  wind_direction_meteorological_50deg_24x24,
  wind_direction_meteorological_51deg_24x24,
  wind_direction_meteorological_52deg_24x24,
  wind_direction_meteorological_53deg_24x24,
  wind_direction_meteorological_54deg_24x24,
  wind_direction_meteorological_55deg_24x24,
  wind_direction_meteorological_56deg_24x24,
  wind_direction_meteorological_57deg_24x24,
  wind_direction_meteorological_58deg_24x24,
  wind_direction_meteorological_59deg_24x24,
  wind_direction_meteorological_60deg_24x24,
  wind_direction_meteorological_61deg_24x24,
  wind_direction_meteorological_62deg_24x24,
  wind_direction_meteorological_63deg_24x24,
  wind_direction_meteorological_64deg_24x24,
  wind_direction_meteorological_65deg_24x24,
  wind_direction_meteorological_66deg_24x24,
  wind_direction_meteorological_67deg_24x24,
  wind_direction_meteorological_68deg_24x24,
  wind_direction_meteorological_69deg_24x24,
  wind_direction_meteorological_70deg_24x24,
  wind_direction_meteorological_71deg_24x24,
  wind_direction_meteorological_72deg_24x24,
  wind_direction_meteorological_73deg_24x24,
  wind_direction_meteorological_74deg_24x24,
  wind_direction_meteorological_75deg_24x24,
  wind_direction_meteorological_76deg_24x24,
  wind_direction_meteorological_77deg_24x24,
  wind_direction_meteorological_78deg_24x24,
  wind_direction_meteorological_79deg_24x24,
  wind_direction_meteorological_80deg_24x24,
  wind_direction_meteorological_81deg_24x24,
  wind_direction_meteorological_82deg_24x24,
  wind_direction_meteorological_83deg_24x24,
  wind_direction_meteorological_84deg_24x24,
  wind_direction_meteorological_85deg_24x24,
  wind_direction_meteorological_86deg_24x24,
  wind_direction_meteorological_87deg_24x24,
  wind_direction_meteorological_88deg_24x24,
  wind_direction_meteorological_89deg_24x24,
  wind_direction_meteorological_90deg_24x24,
  wind_direction_meteorological_91deg_24x24,
  wind_direction_meteorological_92deg_24x24,
  wind_direction_meteorological_93deg_24x24,
  wind_direction_meteorological_94deg_24x24,
  wind_direction_meteorological_95deg_24x24,
  wind_direction_meteorological_96deg_24x24,
  wind_direction_meteorological_97deg_24x24,
  wind_direction_meteorological_98deg_24x24,
  wind_direction_meteorological_99deg_24x24,
  wind_direction_meteorological_100deg_24x24,
  wind_direction_meteorological_101deg_24x24,
  wind_direction_meteorological_102deg_24x24,
  wind_direction_meteorological_103deg_24x24,
  wind_direction_meteorological_104deg_24x24,
  wind_direction_meteorological_105deg_24x24,
  wind_direction_meteorological_106deg_24x24,
  wind_direction_meteorological_107deg_24x24,
  wind_direction_meteorological_108deg_24x24,
  wind_direction_meteorological_109deg_24x24,
  wind_direction_meteorological_110deg_24x24,
  wind_direction_meteorological_111deg_24x24,
  wind_direction_meteorological_112deg_24x24,
  wind_direction_meteorological_113deg_24x24,
  wind_direction_meteorological_114deg_24x24,
  wind_direction_meteorological_115deg_24x24,
  wind_direction_meteorological_116deg_24x24,
  wind_direction_meteorological_117deg_24x24,
  wind_direction_meteorological_118deg_24x24,
  wind_direction_meteorological_119deg_24x24,
  wind_direction_meteorological_120deg_24x24,
  wind_direction_meteorological_121deg_24x24,
  wind_direction_meteorological_122deg_24x24,
  wind_direction_meteorological_123deg_24x24,
  wind_direction_meteorological_124deg_24x24,
  wind_direction_meteorological_125deg_24x24,
  wind_direction_meteorological_126deg_24x24,
  wind_direction_meteorological_127deg_24x24,
  wind_direction_meteorological_128deg_24x24,
  wind_direction_meteorological_129deg_24x24,
  wind_direction_meteorological_130deg_24x24,
  wind_direction_meteorological_131deg_24x24,
  wind_direction_meteorological_132deg_24x24,
  wind_direction_meteorological_133deg_24x24,
  wind_direction_meteorological_134deg_24x24,
  wind_direction_meteorological_135deg_24x24,
  wind_direction_meteorological_136deg_24x24,
  wind_direction_meteorological_137deg_24x24,
  wind_direction_meteorological_138deg_24x24,
  wind_direction_meteorological_139deg_24x24,
  wind_direction_meteorological_140deg_24x24,
  wind_direction_meteorological_141deg_24x24,
  wind_direction_meteorological_142deg_24x24,
  wind_direction_meteorological_143deg_24x24,
  wind_direction_meteorological_144deg_24x24,
  wind_direction_meteorological_145deg_24x24,
  wind_direction_meteorological_146deg_24x24,
  wind_direction_meteorological_147deg_24x24,
  wind_direction_meteorological_148deg_24x24,
  wind_direction_meteorological_149deg_24x24,
  wind_direction_meteorological_150deg_24x24,
  wind_direction_meteorological_151deg_24x24,
  wind_direction_meteorological_152deg_24x24,
  wind_direction_meteorological_153deg_24x24,
  wind_direction_meteorological_154deg_24x24,
  wind_direction_meteorological_155deg_24x24,
  wind_direction_meteorological_156deg_24x24,
  wind_direction_meteorological_157deg_24x24,
  wind_direction_meteorological_158deg_24x24,
  wind_direction_meteorological_159deg_24x24,
  wind_direction_meteorological_160deg_24x24,
  wind_direction_meteorological_161deg_24x24,
  wind_direction_meteorological_162deg_24x24,
  wind_direction_meteorological_163deg_24x24,
  wind_direction_meteorological_164deg_24x24,
  wind_direction_meteorological_165deg_24x24,
  wind_direction_meteorological_166deg_24x24,
  wind_direction_meteorological_167deg_24x24,
  wind_direction_meteorological_168deg_24x24,
  wind_direction_meteorological_169deg_24x24,
  wind_direction_meteorological_170deg_24x24,
  wind_direction_meteorological_171deg_24x24,
  wind_direction_meteorological_172deg_24x24,
  wind_direction_meteorological_173deg_24x24,
  wind_direction_meteorological_174deg_24x24,
  wind_direction_meteorological_175deg_24x24,
  wind_direction_meteorological_176deg_24x24,
  wind_direction_meteorological_177deg_24x24,
  wind_direction_meteorological_178deg_24x24,
  wind_direction_meteorological_179deg_24x24,
  wind_direction_meteorological_180deg_24x24,
  wind_direction_meteorological_181deg_24x24,
  wind_direction_meteorological_182deg_24x24,
  wind_direction_meteorological_183deg_24x24,
  wind_direction_meteorological_184deg_24x24,
  wind_direction_meteorological_185deg_24x24,
  wind_direction_meteorological_186deg_24x24,
  wind_direction_meteorological_187deg_24x24,
  wind_direction_meteorological_188deg_24x24,
  wind_direction_meteorological_189deg_24x24,
  wind_direction_meteorological_190deg_24x24,
  wind_direction_meteorological_191deg_24x24,
  wind_direction_meteorological_192deg_24x24,
  wind_direction_meteorological_193deg_24x24,
  wind_direction_meteorological_194deg_24x24,
  wind_direction_meteorological_195deg_24x24,
  wind_direction_meteorological_196deg_24x24,
  wind_direction_meteorological_197deg_24x24,
  wind_direction_meteorological_198deg_24x24,
  wind_direction_meteorological_199deg_24x24,
  wind_direction_meteorological_200deg_24x24,
  wind_direction_meteorological_201deg_24x24,
  wind_direction_meteorological_202deg_24x24,
  wind_direction_meteorological_203deg_24x24,
  wind_direction_meteorological_204deg_24x24,
  wind_direction_meteorological_205deg_24x24,
  wind_direction_meteorological_206deg_24x24,
  wind_direction_meteorological_207deg_24x24,
  wind_direction_meteorological_208deg_24x24,
  wind_direction_meteorological_209deg_24x24,
  wind_direction_meteorological_210deg_24x24,
  wind_direction_meteorological_211deg_24x24,
  wind_direction_meteorological_212deg_24x24,
  wind_direction_meteorological_213deg_24x24,
  wind_direction_meteorological_214deg_24x24,
  wind_direction_meteorological_215deg_24x24,
  wind_direction_meteorological_216deg_24x24,
  wind_direction_meteorological_217deg_24x24,
  wind_direction_meteorological_218deg_24x24,
  wind_direction_meteorological_219deg_24x24,
  wind_direction_meteorological_220deg_24x24,
  wind_direction_meteorological_221deg_24x24,
  wind_direction_meteorological_222deg_24x24,
  wind_direction_meteorological_223deg_24x24,
  wind_direction_meteorological_224deg_24x24,
  wind_direction_meteorological_225deg_24x24,
  wind_direction_meteorological_226deg_24x24,
  wind_direction_meteorological_227deg_24x24,
  wind_direction_meteorological_228deg_24x24,
  wind_direction_meteorological_229deg_24x24,
  wind_direction_meteorological_230deg_24x24,
  wind_direction_meteorological_231deg_24x24,
  wind_direction_meteorological_232deg_24x24,
  wind_direction_meteorological_233deg_24x24,
  wind_direction_meteorological_234deg_24x24,
  wind_direction_meteorological_235deg_24x24,
  wind_direction_meteorological_236deg_24x24,
  wind_direction_meteorological_237deg_24x24,
  wind_direction_meteorological_238deg_24x24,
  wind_direction_meteorological_239deg_24x24,
  wind_direction_meteorological_240deg_24x24,
  wind_direction_meteorological_241deg_24x24,
  wind_direction_meteorological_242deg_24x24,
  wind_direction_meteorological_243deg_24x24,
  wind_direction_meteorological_244deg_24x24,
  wind_direction_meteorological_245deg_24x24,
  wind_direction_meteorological_246deg_24x24,
  wind_direction_meteorological_247deg_24x24,
  wind_direction_meteorological_248deg_24x24,
  wind_direction_meteorological_249deg_24x24,
  wind_direction_meteorological_250deg_24x24,
  wind_direction_meteorological_251deg_24x24,
  wind_direction_meteorological_252deg_24x24,
  wind_direction_meteorological_253deg_24x24,
  wind_direction_meteorological_254deg_24x24,
  wind_direction_meteorological_255deg_24x24,
  wind_direction_meteorological_256deg_24x24,
  wind_direction_meteorological_257deg_24x24,
  wind_direction_meteorological_258deg_24x24,
  wind_direction_meteorological_259deg_24x24,
  wind_direction_meteorological_260deg_24x24,
  wind_direction_meteorological_261deg_24x24,
  wind_direction_meteorological_262deg_24x24,
  wind_direction_meteorological_263deg_24x24,
  wind_direction_meteorological_264deg_24x24,
  wind_direction_meteorological_265deg_24x24,
  wind_direction_meteorological_266deg_24x24,
  wind_direction_meteorological_267deg_24x24,
  wind_direction_meteorological_268deg_24x24,
  wind_direction_meteorological_269deg_24x24,
  wind_direction_meteorological_270deg_24x24,
  wind_direction_meteorological_271deg_24x24,
  wind_direction_meteorological_272deg_24x24,
  wind_direction_meteorological_273deg_24x24,
  wind_direction_meteorological_274deg_24x24,
  wind_direction_meteorological_275deg_24x24,
  wind_direction_meteorological_276deg_24x24,
  wind_direction_meteorological_277deg_24x24,
  wind_direction_meteorological_278deg_24x24,
  wind_direction_meteorological_279deg_24x24,
  wind_direction_meteorological_280deg_24x24,
  wind_direction_meteorological_281deg_24x24,
  wind_direction_meteorological_282deg_24x24,
  wind_direction_meteorological_283deg_24x24,
  wind_direction_meteorological_284deg_24x24,
  wind_direction_meteorological_285deg_24x24,
  wind_direction_meteorological_286deg_24x24,
  wind_direction_meteorological_287deg_24x24,
  wind_direction_meteorological_288deg_24x24,
  wind_direction_meteorological_289deg_24x24,
  wind_direction_meteorological_290deg_24x24,
  wind_direction_meteorological_291deg_24x24,
  wind_direction_meteorological_292deg_24x24,
  wind_direction_meteorological_293deg_24x24,
  wind_direction_meteorological_294deg_24x24,
  wind_direction_meteorological_295deg_24x24,
  wind_direction_meteorological_296deg_24x24,
  wind_direction_meteorological_297deg_24x24,
  wind_direction_meteorological_298deg_24x24,
  wind_direction_meteorological_299deg_24x24,
  wind_direction_meteorological_300deg_24x24,
  wind_direction_meteorological_301deg_24x24,
  wind_direction_meteorological_302deg_24x24,
  wind_direction_meteorological_303deg_24x24,
  wind_direction_meteorological_304deg_24x24,
  wind_direction_meteorological_305deg_24x24,
  wind_direction_meteorological_306deg_24x24,
  wind_direction_meteorological_307deg_24x24,
  wind_direction_meteorological_308deg_24x24,
  wind_direction_meteorological_309deg_24x24,
  wind_direction_meteorological_310deg_24x24,
  wind_direction_meteorological_311deg_24x24,
  wind_direction_meteorological_312deg_24x24,
  wind_direction_meteorological_313deg_24x24,
  wind_direction_meteorological_314deg_24x24,
  wind_direction_meteorological_315deg_24x24,
  wind_direction_meteorological_316deg_24x24,
  wind_direction_meteorological_317deg_24x24,
  wind_direction_meteorological_318deg_24x24,
  wind_direction_meteorological_319deg_24x24,
  wind_direction_meteorological_320deg_24x24,
  wind_direction_meteorological_321deg_24x24,
  wind_direction_meteorological_322deg_24x24,
  wind_direction_meteorological_323deg_24x24,
  wind_direction_meteorological_324deg_24x24,
  wind_direction_meteorological_325deg_24x24,
  wind_direction_meteorological_326deg_24x24,
  wind_direction_meteorological_327deg_24x24,
  wind_direction_meteorological_328deg_24x24,
  wind_direction_meteorological_329deg_24x24,
  wind_direction_meteorological_330deg_24x24,
  wind_direction_meteorological_331deg_24x24,
  wind_direction_meteorological_332deg_24x24,
  wind_direction_meteorological_333deg_24x24,
  wind_direction_meteorological_334deg_24x24,
  wind_direction_meteorological_335deg_24x24,
  wind_direction_meteorological_336deg_24x24,
  wind_direction_meteorological_337deg_24x24,
  wind_direction_meteorological_338deg_24x24,
  wind_direction_meteorological_339deg_24x24,
  wind_direction_meteorological_340deg_24x24,
  wind_direction_meteorological_341deg_24x24,
  wind_direction_meteorological_342deg_24x24,
  wind_direction_meteorological_343deg_24x24,
  wind_direction_meteorological_344deg_24x24,
  wind_direction_meteorological_345deg_24x24,
  wind_direction_meteorological_346deg_24x24,
  wind_direction_meteorological_347deg_24x24,
  wind_direction_meteorological_348deg_24x24,
  wind_direction_meteorological_349deg_24x24,
  wind_direction_meteorological_350deg_24x24,
  wind_direction_meteorological_351deg_24x24,
  wind_direction_meteorological_352deg_24x24,
  wind_direction_meteorological_353deg_24x24,
  wind_direction_meteorological_354deg_24x24,
  wind_direction_meteorological_355deg_24x24,
  wind_direction_meteorological_356deg_24x24,
  wind_direction_meteorological_357deg_24x24,
  wind_direction_meteorological_358deg_24x24,
  wind_direction_meteorological_359deg_24x24};
#endif // end WIND_ICONS_360

/* Returns a 24x24 wind direction icon bitmap for angles 0 to 359 degrees
 * Parameter is meteorological wind direction, arrow points in the direction the
 * wind is going.
 */
const uint8_t *getWindBitmap24(int windDeg)
{
  windDeg %= 360; // enforce domain
  // number of directions
  int n = sizeof(wind_direction_icon_arr)
          / sizeof(wind_direction_icon_arr[0]);
  int arr_offset = (int) ( (windDeg + (360 / n / 2)) % 360 )
                         / ( 360 / (float) n );

  return wind_direction_icon_arr[arr_offset];
} // end getWindBitmap24

/* Returns a pointer to a string that expresses the Compass Point Notation (CPN)
 * of the given windDeg.
 *
 *   PRECISION                  #     ERROR   EXAMPLE
 *   Cardinal                   4  ±45.000°   E
 *   Intercardinal (Ordinal)    8  ±22.500°   NE
 *   Secondary Intercardinal   16  ±11.250°   NNE
 *   Tertiary Intercardinal    32   ±5.625°   NbE
 */
const char *getCompassPointNotation(int windDeg)
{
#if defined(WIND_INDICATOR_CPN_CARDINAL)
  const int precision = 4;
#elif defined(WIND_INDICATOR_CPN_INTERCARDINAL)
  const int precision = 8;
#elif defined(WIND_INDICATOR_CPN_SECONDARY_INTERCARDINAL)
  const int precision = 16;
#elif defined(WIND_INDICATOR_CPN_TERTIARY_INTERCARDINAL)
  const int precision = 32;
#else
  const int precision = 4;
#endif

  windDeg %= 360; // enforce domain
  int arr_offset = (int) (windDeg / ( 360 / (float) precision ))
                         * ( 32 / precision) ;

  return COMPASS_POINT_NOTATION[arr_offset];
} // end getCompassPointNotation

/* This function returns a pointer to a string representing the meaning for a
 * HTTP response status code or an arduino client error code.
 * ArduinoJson DeserializationError codes are also included here and are given a
 * negative 100 offset to distinguish them from other client error codes.
 *
 * HTTP response status codes [100, 599]
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
 *
 * HTTP client errors [0, -255]
 * https://github.com/espressif/arduino-esp32/blob/master/libraries/HTTPClient/src/HTTPClient.h
 *
 * ArduinoJson DeserializationError codes [-256, -511]
 * https://arduinojson.org/v6/api/misc/deserializationerror/
 *
 * WiFi Status codes [-512, -767]
 * https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/src/WiFiType.h
 */
const char *getHttpResponsePhrase(int code)
{
  switch (code)
  {
  // 1xx - Informational Responses
  case 100: return TXT_HTTP_RESPONSE_100;
  case 101: return TXT_HTTP_RESPONSE_101;
  case 102: return TXT_HTTP_RESPONSE_102;
  case 103: return TXT_HTTP_RESPONSE_103;

  // 2xx - Successful Responses
  case 200: return TXT_HTTP_RESPONSE_200;
  case 201: return TXT_HTTP_RESPONSE_201;
  case 202: return TXT_HTTP_RESPONSE_202;
  case 203: return TXT_HTTP_RESPONSE_203;
  case 204: return TXT_HTTP_RESPONSE_204;
  case 205: return TXT_HTTP_RESPONSE_205;
  case 206: return TXT_HTTP_RESPONSE_206;
  case 207: return TXT_HTTP_RESPONSE_207;
  case 208: return TXT_HTTP_RESPONSE_208;
  case 226: return TXT_HTTP_RESPONSE_226;

  // 3xx - Redirection Responses
  case 300: return TXT_HTTP_RESPONSE_300;
  case 301: return TXT_HTTP_RESPONSE_301;
  case 302: return TXT_HTTP_RESPONSE_302;
  case 303: return TXT_HTTP_RESPONSE_303;
  case 304: return TXT_HTTP_RESPONSE_304;
  case 305: return TXT_HTTP_RESPONSE_305;
  case 307: return TXT_HTTP_RESPONSE_307;
  case 308: return TXT_HTTP_RESPONSE_308;

  // 4xx - Client Error Responses
  case 400: return TXT_HTTP_RESPONSE_400;
  case 401: return TXT_HTTP_RESPONSE_401;
  case 402: return TXT_HTTP_RESPONSE_402;
  case 403: return TXT_HTTP_RESPONSE_403;
  case 404: return TXT_HTTP_RESPONSE_404;
  case 405: return TXT_HTTP_RESPONSE_405;
  case 406: return TXT_HTTP_RESPONSE_406;
  case 407: return TXT_HTTP_RESPONSE_407;
  case 408: return TXT_HTTP_RESPONSE_408;
  case 409: return TXT_HTTP_RESPONSE_409;
  case 410: return TXT_HTTP_RESPONSE_410;
  case 411: return TXT_HTTP_RESPONSE_411;
  case 412: return TXT_HTTP_RESPONSE_412;
  case 413: return TXT_HTTP_RESPONSE_413;
  case 414: return TXT_HTTP_RESPONSE_414;
  case 415: return TXT_HTTP_RESPONSE_415;
  case 416: return TXT_HTTP_RESPONSE_416;
  case 417: return TXT_HTTP_RESPONSE_417;
  case 418: return TXT_HTTP_RESPONSE_418;
  case 421: return TXT_HTTP_RESPONSE_421;
  case 422: return TXT_HTTP_RESPONSE_422;
  case 423: return TXT_HTTP_RESPONSE_423;
  case 424: return TXT_HTTP_RESPONSE_424;
  case 425: return TXT_HTTP_RESPONSE_425;
  case 426: return TXT_HTTP_RESPONSE_426;
  case 428: return TXT_HTTP_RESPONSE_428;
  case 429: return TXT_HTTP_RESPONSE_429;
  case 431: return TXT_HTTP_RESPONSE_431;
  case 451: return TXT_HTTP_RESPONSE_451;

  // 5xx - Server Error Responses
  case 500: return TXT_HTTP_RESPONSE_500;
  case 501: return TXT_HTTP_RESPONSE_501;
  case 502: return TXT_HTTP_RESPONSE_502;
  case 503: return TXT_HTTP_RESPONSE_503;
  case 504: return TXT_HTTP_RESPONSE_504;
  case 505: return TXT_HTTP_RESPONSE_505;
  case 506: return TXT_HTTP_RESPONSE_506;
  case 507: return TXT_HTTP_RESPONSE_507;
  case 508: return TXT_HTTP_RESPONSE_508;
  case 510: return TXT_HTTP_RESPONSE_510;
  case 511: return TXT_HTTP_RESPONSE_511;

  // HTTP client errors [0, -255]
  case HTTPC_ERROR_CONNECTION_REFUSED:  return TXT_HTTPC_ERROR_CONNECTION_REFUSED;
  case HTTPC_ERROR_SEND_HEADER_FAILED:  return TXT_HTTPC_ERROR_SEND_HEADER_FAILED;
  case HTTPC_ERROR_SEND_PAYLOAD_FAILED: return TXT_HTTPC_ERROR_SEND_PAYLOAD_FAILED;
  case HTTPC_ERROR_NOT_CONNECTED:       return TXT_HTTPC_ERROR_NOT_CONNECTED;
  case HTTPC_ERROR_CONNECTION_LOST:     return TXT_HTTPC_ERROR_CONNECTION_LOST;
  case HTTPC_ERROR_NO_STREAM:           return TXT_HTTPC_ERROR_NO_STREAM;
  case HTTPC_ERROR_NO_HTTP_SERVER:      return TXT_HTTPC_ERROR_NO_HTTP_SERVER;
  case HTTPC_ERROR_TOO_LESS_RAM:        return TXT_HTTPC_ERROR_TOO_LESS_RAM;
  case HTTPC_ERROR_ENCODING:            return TXT_HTTPC_ERROR_ENCODING;
  case HTTPC_ERROR_STREAM_WRITE:        return TXT_HTTPC_ERROR_STREAM_WRITE;
  case HTTPC_ERROR_READ_TIMEOUT:        return TXT_HTTPC_ERROR_READ_TIMEOUT;

  // ArduinoJson DeserializationError codes  [-256, -511]
  case -256 - (DeserializationError::Code::Ok):              return TXT_DESERIALIZATION_ERROR_OK;
  case -256 - (DeserializationError::Code::EmptyInput):      return TXT_DESERIALIZATION_ERROR_EMPTY_INPUT;
  case -256 - (DeserializationError::Code::IncompleteInput): return TXT_DESERIALIZATION_ERROR_INCOMPLETE_INPUT;
  case -256 - (DeserializationError::Code::InvalidInput):    return TXT_DESERIALIZATION_ERROR_INVALID_INPUT;
  case -256 - (DeserializationError::Code::NoMemory):        return TXT_DESERIALIZATION_ERROR_NO_MEMORY;
  case -256 - (DeserializationError::Code::TooDeep):         return TXT_DESERIALIZATION_ERROR_TOO_DEEP;

  // WiFi Status codes [-512, -767]
  case -512 - WL_NO_SHIELD:       return TXT_WL_NO_SHIELD;
  // case -512 - WL_STOPPED:       return TXT_WL_STOPPED; // future
  case -512 - WL_IDLE_STATUS:     return TXT_WL_IDLE_STATUS;
  case -512 - WL_NO_SSID_AVAIL:   return TXT_WL_NO_SSID_AVAIL;
  case -512 - WL_SCAN_COMPLETED:  return TXT_WL_SCAN_COMPLETED;
  case -512 - WL_CONNECTED:       return TXT_WL_CONNECTED;
  case -512 - WL_CONNECT_FAILED:  return TXT_WL_CONNECT_FAILED;
  case -512 - WL_CONNECTION_LOST: return TXT_WL_CONNECTION_LOST;
  case -512 - WL_DISCONNECTED:    return TXT_WL_DISCONNECTED;

  default:  return "";
  }
} // end getHttpResponsePhrase


/* This function returns a pointer to a string representing the meaning for a
 * WiFi status (wl_status_t).
 *
 * wl_status_t type definition
 * https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/src/WiFiType.h
 */
const char *getWifiStatusPhrase(wl_status_t status)
{
  switch (status)
  {
  case WL_NO_SHIELD:       return TXT_WL_NO_SHIELD;
  // case WL_STOPPED:       return TXT_WL_STOPPED; // future
  case WL_IDLE_STATUS:     return TXT_WL_IDLE_STATUS;
  case WL_NO_SSID_AVAIL:   return TXT_WL_NO_SSID_AVAIL;
  case WL_SCAN_COMPLETED:  return TXT_WL_SCAN_COMPLETED;
  case WL_CONNECTED:       return TXT_WL_CONNECTED;
  case WL_CONNECT_FAILED:  return TXT_WL_CONNECT_FAILED;
  case WL_CONNECTION_LOST: return TXT_WL_CONNECTION_LOST;
  case WL_DISCONNECTED:    return TXT_WL_DISCONNECTED;

  default:  return "";
  }
} // end getWifiStatusPhrase

/* This function sets the builtin LED to LOW and disables it even during deep
 * sleep.
 */
void disableBuiltinLED()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(LED_BUILTIN));
  gpio_deep_sleep_hold_en();
  return;
} // end disableBuiltinLED

/* Maps WMO weather codes to appropriate icon bitmaps.
 * WMO codes: https://open-meteo.com/en/docs
 *
 * code: WMO weather code (0-99)
 * is_day: true for day icons, false for night
 * BitmapSize: icon size as template parameter (32, 64, 196)
 */
template <int BitmapSize>
const uint8_t *getIconFromWMO(int code, bool is_day)
{
  switch (code)
  {
  // Clear sky
  case 0:
    if (is_day) { return getBitmap(wi_day_sunny, BitmapSize); }
    return getBitmap(wi_night_clear, BitmapSize);

  // Mainly clear
  case 1:
    if (is_day) { return getBitmap(wi_day_sunny_overcast, BitmapSize); }
    return getBitmap(wi_night_alt_partly_cloudy, BitmapSize);

  // Partly cloudy
  case 2:
    if (is_day) { return getBitmap(wi_day_cloudy, BitmapSize); }
    return getBitmap(wi_night_alt_cloudy, BitmapSize);

  // Overcast
  case 3:
    return getBitmap(wi_cloudy, BitmapSize);

  // Fog and depositing rime fog
  case 45:
  case 48:
    if (is_day) { return getBitmap(wi_day_fog, BitmapSize); }
    return getBitmap(wi_night_fog, BitmapSize);

  // Drizzle: Light, moderate, dense
  case 51:
  case 53:
  case 55:
    if (is_day) { return getBitmap(wi_day_showers, BitmapSize); }
    return getBitmap(wi_night_alt_showers, BitmapSize);

  // Freezing drizzle: Light, dense
  case 56:
  case 57:
    if (is_day) { return getBitmap(wi_day_rain_mix, BitmapSize); }
    return getBitmap(wi_night_alt_rain_mix, BitmapSize);

  // Rain: Slight, moderate, heavy
  case 61:
  case 63:
  case 65:
    if (is_day) { return getBitmap(wi_day_rain, BitmapSize); }
    return getBitmap(wi_night_alt_rain, BitmapSize);

  // Freezing rain: Light, heavy
  case 66:
  case 67:
    if (is_day) { return getBitmap(wi_day_rain_mix, BitmapSize); }
    return getBitmap(wi_night_alt_rain_mix, BitmapSize);

  // Snow fall: Slight, moderate, heavy
  case 71:
  case 73:
  case 75:
  case 77: // Snow grains
    if (is_day) { return getBitmap(wi_day_snow, BitmapSize); }
    return getBitmap(wi_night_alt_snow, BitmapSize);

  // Rain showers: Slight, moderate, violent
  case 80:
  case 81:
  case 82:
    if (is_day) { return getBitmap(wi_day_showers, BitmapSize); }
    return getBitmap(wi_night_alt_showers, BitmapSize);

  // Snow showers: Slight, heavy
  case 85:
  case 86:
    if (is_day) { return getBitmap(wi_day_snow, BitmapSize); }
    return getBitmap(wi_night_alt_snow, BitmapSize);

  // Thunderstorm: Slight or moderate, with hail
  case 95:
  case 96:
  case 99:
    if (is_day) { return getBitmap(wi_day_thunderstorm, BitmapSize); }
    return getBitmap(wi_night_alt_thunderstorm, BitmapSize);

  // Unknown code - return N/A icon
  default:
    return getBitmap(wi_na, BitmapSize);
  }
} // end getIconFromWMO
