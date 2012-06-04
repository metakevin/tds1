/******************************************************************************
* File:              fuelpressure5v.c
* Author:            Kevin Day
* Date:              January, 2007
* Description:       
*                    
*                    
* Copyright (c) 2007 Kevin Day
* 
*     This program is free software: you can redistribute it and/or modify
*     it under the terms of the GNU General Public License as published by
*     the Free Software Foundation, either version 3 of the License, or
*     (at your option) any later version.
*
*     This program is distributed in the hope that it will be useful,
*     but WITHOUT ANY WARRANTY; without even the implied warranty of
*     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*     GNU General Public License for more details.
*
*     You should have received a copy of the GNU General Public License
*     along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*******************************************************************************/

#include <avr/io.h>
#include "types.h"
#include "timers.h"
#include "adc.h"
#include "userinterface.h"
#include "comms_generic.h"
#include "iat.h"
#include "audiradio.h"
#include "persist.h"
#include "output.h"
#include "datalogger.h"
#include "boost.h"

#define FP_PIN 3

u8 fp_display_func(ui_mode_t mode, ui_display_event_t event);
static inline u8 fp_config_func(ui_mode_t mode, ui_display_event_t event);
void fp_adc_callback(adc_context_t *ctx);

#define SAMPLES_PER_AVG 64
//#define SAMPLES_PER_AVG 8

#define FP_UPDATE_PERIOD   MS_TO_TICK(250)

u16 sample_accumulator;
u16 current_fp_accum;
u8  sample_counter;

union {
    struct {
        u8  absolute : 1;
        u8  relative : 1;
        u8  trough   : 1;
    } metric;
    u8 you8;
} fp_display_mode;

u16 fp_trough_value;
u8 fp_trough_counter;
timerentry_t fp_update_timer;
#define FP_UPDATE_PERIOD MS_TO_TICK(250)
#define DEFAULT_PEAK_AGE                4*5 /* in units of UPDATE_MS */
#define LONG_PEAK_AGE                   4*10
extern u8 peak_hold_time_10s;
#define FP_TROUGH_AGE (peak_hold_time_10s?LONG_PEAK_AGE:DEFAULT_PEAK_AGE)
void fp_update_callback(timerentry_t *ctx)
{
    if (fp_trough_counter < FP_TROUGH_AGE)
    {
        ++fp_trough_counter;
    }
    register_timer_callback(&fp_update_timer, FP_UPDATE_PERIOD, 
            fp_update_callback, 0);
}

void fp_init(adc_context_t *adc_context)
{
    adc_context->pin      = FP_PIN;
    adc_context->ref      = ADC_AVCC;
    adc_context->enabled  = 1;
    adc_context->callback = fp_adc_callback;

    fp_display_mode.you8 = load_persist_data(PDATA_FUELPRESSURE_UNITS);

    register_display_mode(MODE_FP_ABSOLUTE, fp_display_func); 
    register_display_mode(MODE_FP_RELATIVE, fp_display_func);  
    register_display_mode(MODE_FP_RELATIVE_TROUGH, fp_display_func);  

    fp_update_callback(0);
}

void fp_adc_callback(adc_context_t *ctx)
{
    sample_accumulator += ctx->sample;
    ++sample_counter;
    if (sample_counter >= SAMPLES_PER_AVG)
    {
        sample_counter = 0;
        current_fp_accum = sample_accumulator;
        sample_accumulator = 0;
        log_data_sample(DATA_TYPE_FP_VOLTS, 2, (u8*)&current_fp_accum);
    }
}

/* Honeywell ML150
 * 0.5V = 0 PSIS
 * 4.5V = 150 PSIS  
 *
 *  4 volt span.  150/4 = 37.5PSI/volt
 *  1 PSI = 0.0266666V
 *  1 PSI = 5.4613333 LSBs 
 *  or 349.52533333 LSBs oversampled 64x LSBs (oLSBs)
 *
 *  The offset at 0.5V is 6553.6 oLSBs
 *
 *  So (oLSBs - 6553.60) /349.53 will give integral PSI.
 *
 *
 * 4 Bar gauge @ std. atmosphere = 58.0150951*0.02666666=0.5+1.5470692V = 26831 adc64
 *
 * 0 PSIS = 14.7 PSIA
 *
 *  
 *  In kPa:
 *
 *  0.5V = 101.325 kPa
 *  4.5V = 1135.539 kPa
 *  4 volt span.  1034.214/4 = 258.554 kPa/volt
 *  1 kPa = 0.003867664V
 *  1 kPa = 0.7921 LSBs
 *  or 50.69 LSBs oversampled 64x (oLSBs)
 *  The offset at 0.5V is 6553.6 oLSBs.
 *  To convert from sealed to absolute pressure, add 101.325
 *  So kPa = (oLSBs - 6553.6) / 50.69 + 101.325
 *
 *
 *
 *
 *
 * 
 */
#define ZERO_OFFSET_10Q6 6554L
#define PSI_TENTHS_DIVISOR_10Q6 35L
#define KPA_TENTHS_DIVISOR_10Q6 5



u8 fp_display_func(ui_mode_t mode, ui_display_event_t event)
{
    u16 vofs10q6 = current_fp_accum;
    u8 is_metric = 0;
    center_letter_t cflags = CENTER_NONE;
    number_display_flags_t dflags = DFLAGS_NONE;

    if (event >= IN_CONFIG)
        return fp_config_func(mode, event);

    if (((mode == MODE_FP_RELATIVE) && fp_display_mode.metric.relative) ||
        (mode == MODE_FP_ABSOLUTE && fp_display_mode.metric.absolute) ||
        (mode == MODE_FP_RELATIVE_TROUGH && fp_display_mode.metric.trough))
    {
        is_metric = 1;
    }

    if (vofs10q6 > ZERO_OFFSET_10Q6)
    {
        vofs10q6 -= ZERO_OFFSET_10Q6;
    }
    else
    {
        vofs10q6 = 0;
    }

    /* Convert voltage to mbar */
    /* This is an empirically derived value which estimates
     * the rounding error in the /5 +1013 below. */
    u16 fixup = vofs10q6 / 370;
    
    vofs10q6 /= 5;
    vofs10q6 += 1013;
    vofs10q6 -= fixup;

    if (mode == MODE_FP_RELATIVE || mode == MODE_FP_RELATIVE_TROUGH)
    {
        vofs10q6 -= boost_get_mbar();
        cflags = CENTER_D;
        if (mode == MODE_FP_RELATIVE_TROUGH)
        {
            if (vofs10q6 < fp_trough_value)
            {
                fp_trough_value = vofs10q6;
                fp_trough_counter = 0;
            }
            if (fp_trough_counter < FP_TROUGH_AGE)
            {
                dflags |= CENTER_FLASH;
                vofs10q6 = fp_trough_value;
            }
            else
            {
                fp_trough_value = vofs10q6 - 10;
            }
        }
    }
    else
    {
        /* Display absolute pressure in PSIS, not PSIA... */
        vofs10q6 -= 1013;
    }

    if (is_metric)
    {
        if (vofs10q6 > 9999)
        {
            vofs10q6 = 9999;
        }
        output_number(mode, vofs10q6, cflags, dflags);
    }
    else
    {
        u32 psix10 = vofs10q6 * 10L;
        psix10 /= 69L;
        output_number(mode, psix10, cflags, dflags);        
    }

    return 0;
}

static inline u8 fp_config_func(ui_mode_t mode, ui_display_event_t event)
{
    if (event == CONFIG_SHORT_PRESS)
    {
        if (mode == MODE_FP_RELATIVE)
        {
            fp_display_mode.metric.relative = !fp_display_mode.metric.relative;
        }
        else if (mode == MODE_FP_RELATIVE_TROUGH)
        {
            fp_display_mode.metric.trough = !fp_display_mode.metric.trough;
        }
        else
        {
            fp_display_mode.metric.absolute = !fp_display_mode.metric.absolute;
        }
        save_persist_data(PDATA_FUELPRESSURE_UNITS, fp_display_mode.you8);
    }
    else if (event == CONFIG_LONG_PRESS)
    {
        /* done */
        return 0;
    }

    fp_display_func(mode, 0);
    return 1;
}
    
