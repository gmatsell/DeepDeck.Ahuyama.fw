/*
 * Battery monitoring via voltage divider on an analog pin.
 * Updated for ESP-IDF 5.x/6.x ADC oneshot + calibration API.
 */

#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "keyboard_config.h"
#include "battery_monitor.h"

#define TAG "BATTERY"
#define NO_OF_SAMPLES   500

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle = NULL;
static bool cali_enabled = false;

uint32_t voltage = 0;

uint32_t get_battery_level(void) {
    int raw = 0;
    int sum = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        adc_oneshot_read(adc_handle, BATT_PIN, &raw);
        sum += raw;
    }
    sum /= NO_OF_SAMPLES;

    int mv = 0;
    if (cali_enabled) {
        adc_cali_raw_to_voltage(cali_handle, sum, &mv);
    } else {
        mv = sum * 3300 / 4095;
    }
    voltage = (uint32_t)mv;

    uint32_t battery_percent = 0;
    if (voltage > Vout_min) {
        battery_percent = ((voltage - Vout_min) * 100 / (Vout_max - Vout_min));
        if (battery_percent > 100) battery_percent = 100;
    }
    return battery_percent;
}

void init_batt_monitor(void) {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_2_5,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, BATT_PIN, &chan_cfg));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = BATT_PIN,
        .atten    = ADC_ATTEN_DB_2_5,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);
    if (ret == ESP_OK) cali_enabled = true;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_2_5,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle);
    if (ret == ESP_OK) cali_enabled = true;
#endif
    if (!cali_enabled) {
        ESP_LOGW(TAG, "ADC calibration not available, using raw conversion");
    }
}
