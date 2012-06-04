/******************************************************************************
* File:              iat.c
* Author:            Kevin Day
* Date:              April, 2005
* Description:       
*                    
*                    
* Copyright (c) 2005 Kevin Day
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

#define IAT_PIN 1

u8 iat_display_func(ui_mode_t mode, ui_display_event_t event);
static inline u8 iat_config_func(ui_mode_t mode, ui_display_event_t event);
void iat_adc_callback(adc_context_t *ctx);
void iat_update_callback(timerentry_t *ctx);
timerentry_t iat_timer;

/* Due to the voltage divider formed by the thermistor
 * and the ECU resistor, the range of voltages present
 * on the ADC pin is small (16% of full scale @ 2.56 Vref)
 * Therefor collect as many samples as reasonable before
 * presenting the data.  
 * if IAT_10BIT is not defined, then the format is 10Q14 
 * for the accumulator.  Otherwise it is 10Q10,
 * unless USE_DISCRETIONARY_BITFIELDS is not set, in
 * which case the accumulator format depends on
 * SAMPLES_PER_AVG.
 */

#ifdef IAT_1024_SAMPLES
  #define SAMPLES_PER_AVG 1024
  #define ACCUMULATOR_Q   10
#else
  #define SAMPLES_PER_AVG 256
  #define ACCUMULATOR_Q   8
#endif

#define SAMPLES_PER_AVG_INSTANT 512
#define ACCUMULATOR_Q_INSTANT   9
#define SAMPLES_PER_AVG_PEAK    64
#define ACCUMULATOR_Q_PEAK      6

#define IAT_UPDATE_PERIOD   MS_TO_TICK(250)

extern u8 peak_hold_time_10s;
#define DEFAULT_IAT_PEAK_AGE        4*5 /* 5 seconds */
#define LONG_IAT_PEAK_AGE           4*10 /* 5 seconds */
#define IAT_PEAK_AGE                (peak_hold_time_10s?LONG_IAT_PEAK_AGE:DEFAULT_IAT_PEAK_AGE)

#define BLINK_QUARTER_MASK  2
#define BLINK_WRAP 8    // 1000ms -- see display_update_timer

typedef struct {
    u32 sample_accumulator;
    u32 current_iat_accum;
    u16 sample_counter;
    u8  iat_accum_valid;
    u8  peak_counter;
    u32 peak_value;
//    u32 longterm_accum;
    u8  blink_counter;
    u8 peak_valid;
    u8 accum_mode;
    u8 units_nonmetric;
} iat_context_t;

iat_context_t iat_ctx;

void iat_init(adc_context_t *adc_context)
{
    adc_context->pin      = IAT_PIN;
    //adc_context->ref      = ADC_INT_256;
    adc_context->ref      = ADC_AVCC;
    adc_context->enabled  = 1;
    adc_context->callback = iat_adc_callback;
    
    iat_ctx.units_nonmetric = load_persist_data(PDATA_IAT_UNITS);
    
    register_display_mode(MODE_IAT, iat_display_func);   
    register_display_mode(MODE_IAT_PEAK, iat_display_func);   
    
    iat_update_callback(NULL);
}


void iat_adc_callback(adc_context_t *ctx)
{
    u16 samples_max;

    if (iat_ctx.accum_mode == MODE_IAT)
    {
        samples_max = SAMPLES_PER_AVG_INSTANT;
    }
    else
    {
        samples_max = SAMPLES_PER_AVG_PEAK;
    }

    if(iat_ctx.sample_counter > samples_max)
    {
        iat_ctx.sample_counter = 0;
    }
    
    iat_ctx.sample_accumulator += ctx->sample;
    ++iat_ctx.sample_counter;
    if (iat_ctx.sample_counter == samples_max)
    {
        iat_ctx.sample_counter = 0;
        iat_ctx.current_iat_accum = iat_ctx.sample_accumulator;
        iat_ctx.iat_accum_valid = 1;
        iat_ctx.sample_accumulator = 0;

        if (iat_ctx.current_iat_accum > iat_ctx.peak_value ||
                iat_ctx.peak_counter >= IAT_PEAK_AGE ||
                !iat_ctx.peak_valid)
        {
            iat_ctx.peak_valid = 1;
            iat_ctx.peak_value = iat_ctx.current_iat_accum;
            iat_ctx.peak_counter = 0;
        }        
        log_data_sample(DATA_TYPE_IAT_VOLTS, 2, (u8*)&iat_ctx.current_iat_accum);
    }
}

void iat_update_callback(timerentry_t *ctx)
{
    if (iat_ctx.peak_counter < IAT_PEAK_AGE)
    {
        ++iat_ctx.peak_counter;
    }

    register_timer_callback(&iat_timer, IAT_UPDATE_PERIOD,
            iat_update_callback, 0);
}


/* IAT
 *
 *      Rtherm =  (Vadc * Recu) / (Vin - Vadc)
 *
 *      TempC  =  (Rtherm - 456) / 1.62
 */

/* Vadc ranges from 0..2.56V
 * that requires only 2 bits left of the decimal. 
 * VREF_OVER_1024_16Q16 = (2.56/1024) * 2^16 */
#define VREF_OVER_1024_16Q16     164
#define VREF_5V_OVER_1024_16Q16  320
#define ZERO_C_OFFSET            456
#define RECU_12Q0                2342
#define VIN_3Q0                  5
#define VIN_14Q18                1310720        // 5*2^18
#define SLOPE_16Q16              106168         // 1.62 * 2^16
#define ONE_DEGREE_C_Q18         424673         // 1.62 * 2^18

/* Multiple divisions:
 *
 *      __divmodhi4     : 16 / 16
 *      __divmodsi4     : 32 / 32
 */

u8 iat_display_func(ui_mode_t mode, ui_display_event_t event)
{
    // shift right by 8 corresponds to 2^10 samples per accum (Q10+Q16-8=18)
    u32 Vadc2q18;
    u32 VadcXRecu14q18;
    u16 Rtherm14q0;
    s16 TempC;
    u8  accum_q_shift;

    if (event >= IN_CONFIG)
        return iat_config_func(mode, event);
    
    iat_ctx.blink_counter = (iat_ctx.blink_counter+1)%BLINK_WRAP;

    if (iat_ctx.accum_mode != mode)
    {
        iat_ctx.accum_mode = mode;
        iat_ctx.iat_accum_valid = 0;
		iat_ctx.sample_counter = 0;
		iat_ctx.sample_accumulator = 0;
		iat_ctx.peak_valid = 0;
        return 0;
    }
    
    if (iat_ctx.accum_mode == MODE_IAT)
    {
        accum_q_shift = ACCUMULATOR_Q_INSTANT + 16 - 18;
    }
    else
    {
        accum_q_shift = ACCUMULATOR_Q_PEAK + 16 - 18;
    }    
    
    Vadc2q18 = (iat_ctx.current_iat_accum * VREF_5V_OVER_1024_16Q16) 
        >> accum_q_shift;
        
    VadcXRecu14q18 = Vadc2q18 * RECU_12Q0;
    Rtherm14q0 = VadcXRecu14q18 / (VIN_14Q18 - Vadc2q18);
    TempC = (((s32)Rtherm14q0 - ZERO_C_OFFSET)<<16)/SLOPE_16Q16;

    temp_format_mode_t tmode = TEMP_NORMAL;    
    
    if (mode == MODE_IAT_PEAK && iat_ctx.peak_valid)
    {
        u32 PeakVadc2q18;
        u32 PeakVadcXRecu14q18;
        u16 PeakRtherm14q0;
        s16 PeakTempC;
        
        /* peak */
        PeakVadc2q18 = (iat_ctx.peak_value * VREF_5V_OVER_1024_16Q16)
            >> accum_q_shift;

        PeakVadcXRecu14q18 = PeakVadc2q18 * RECU_12Q0;
        PeakRtherm14q0 = PeakVadcXRecu14q18 / (VIN_14Q18 - PeakVadc2q18);
        PeakTempC = (((s32)PeakRtherm14q0 - ZERO_C_OFFSET)<<16)/SLOPE_16Q16;
        
        if (PeakTempC - TempC >= 5)
        {
            /* Indicate the peak is being held if there is a 5 degree delta */
            tmode |= TEMP_PEAK_HOLD;
        }
        TempC = PeakTempC;        
    }

    if (iat_ctx.units_nonmetric)
    {
        tmode |= TEMP_FAHRENHEIT;
    }

    output_temperature(mode, TempC, tmode);
    return 0;
}

static inline u8 iat_config_func(ui_mode_t mode, ui_display_event_t event)
{
    if (event == CONFIG_SHORT_PRESS)
    {
        iat_ctx.units_nonmetric = 
            !iat_ctx.units_nonmetric;
        save_persist_data(PDATA_IAT_UNITS, iat_ctx.units_nonmetric);
    }
    else if (event == CONFIG_LONG_PRESS)
    {
        /* done */
        return 0;
    }

    iat_display_func(mode, 0);

    /* not done */
    return 1;   
}
    
