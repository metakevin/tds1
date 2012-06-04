/******************************************************************************
* File:              oilpres.c
* Author:            Kevin Day
* Date:              November, 2006
* Description:       
*                    
*                    
* Copyright (c) 2006 Kevin Day
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

//#define OILP_OHMS 1

#include <avr/io.h>
#include "types.h"
#include "timers.h"
#include "adc.h"
#include "userinterface.h"
#include "comms_generic.h"
#include "audiradio.h"
#include "persist.h"
#include "output.h"
#include "boost.h"

#define OILPRES_PIN 2

u8 oilpres_display_func(ui_mode_t mode, ui_display_event_t event);
static inline u8 oilpres_config_func(ui_mode_t mode, ui_display_event_t event);
void oilpres_adc_callback(adc_context_t *ctx);
void oilpres_update_callback(timerentry_t *ctx);

#define SAMPLES_PER_AVG 256
#define ACCUMULATOR_Q   8

#define SAMPLES_PER_AVG_INSTANT 64
#define ACCUMULATOR_Q_INSTANT   6
#define SAMPLES_PER_AVG_PEAK    64
#define ACCUMULATOR_Q_PEAK      6

#define OILPRES_UPDATE_PERIOD   MS_TO_TICK(250)
#define OILPRES_PEAK_AGE        4*5 /* 5 seconds */

#define BLINK_QUARTER_MASK  2
#define BLINK_WRAP 8    // 1000ms -- see display_update_timer

typedef struct {
    u32 sample_accumulator;
    u32 current_oilpres_accum;
    u16 sample_counter;
    u8  oilpres_accum_valid;
    union {
        u8 word;
        struct {
            u8 is_metric : 1;
            u8 is_150psi : 1;
        } bits;
    } dispmode;
} oilpres_context_t;

oilpres_context_t oilpres_ctx;

static inline u8 is_metric()
{
    return oilpres_ctx.dispmode.bits.is_metric;
}
static inline u8 is_150psi()
{
    return oilpres_ctx.dispmode.bits.is_150psi;
}    

#if 0
    /* sum(gaugelut[0..i].x is the gauge reading in PSI at 16*i+10 ohms.  */
    const struct {
    u8 psi100 : 4;
    u8 psi70  : 4;
    } gaugelut[] = {
        {0,   0},    /* 0   */
        {7,   0},    /* 7   */
        {7,   0},    /* 14  */
        {9,   0},    /* 23  */
        {10,  0},     /* 33  */
        {12,  0},     /* 45  */
        {13,  0},     /* 58  */
        {12,  0},     /* 70  */
        {11,  0},     /* 81  */
        {9,   0},     /* 90  */
        {6,   0},     /* 96  */
        {7,   0}    /* 103 */
    };
#else
    /* sum(gaugelut[0..i].x+1) is the gauge reading in PSI at 16*i+10 ohms.  */
    
    const struct {
        u8 psi150 : 4;
        u8 psi80  : 4;
    } gaugelut[] = {
	    {10-1, 6-1},
	    {11-1, 6-1},
	    {13-1, 7-1},
	    {13-1, 6-1},
	    {12-1, 7-1},
	    {13-1, 7-1},
	    {14-1, 7-1},
	    {14-1, 6-1},
	    {15-1, 7-1},
	    {16-1, 7-1},
	    {16-1, 9-1},
	    };
#endif


u8 deltalut(u8 i)
{
    if (is_150psi())
    {
        return gaugelut[i].psi150 + 1;
    }
    else
    {
        return gaugelut[i].psi80 + 1;
    }
}

void oilpres_init(adc_context_t *adc_context)
{
    adc_context->pin      = OILPRES_PIN;
    adc_context->ref      = ADC_AVCC;
    adc_context->enabled  = 1;
    adc_context->callback = oilpres_adc_callback;
    
    oilpres_ctx.dispmode.word = ~load_persist_data(PDATA_OILPRES_UNITS);
    
    register_display_mode(MODE_OILPRES, oilpres_display_func);   
}


void oilpres_adc_callback(adc_context_t *ctx)
{
    oilpres_ctx.sample_accumulator += ctx->sample;
    ++oilpres_ctx.sample_counter;
    if (oilpres_ctx.sample_counter == SAMPLES_PER_AVG_INSTANT)
    {
        oilpres_ctx.sample_counter = 0;
        oilpres_ctx.current_oilpres_accum = oilpres_ctx.sample_accumulator;
        oilpres_ctx.oilpres_accum_valid = 1;
        oilpres_ctx.sample_accumulator = 0;
    }
}

void output_pressure(u8 resistance, u8 mode, s8 psig);

u8 oilpres_display_func(ui_mode_t mode, ui_display_event_t event)
{
#ifdef REVB_PROTO
    const u16 r1 = 390; // value of resistor to +5
    const u16 r2fixed_10q6 = 98*64;
#else
    const u16 r1 = 402; // value of resistor to +5
    const u16 r2fixed_10q6 = 100*64;
#endif

    if (event >= IN_CONFIG)
        return oilpres_config_func(mode, event);
    
    u16 adc_10q6 = oilpres_ctx.current_oilpres_accum;
 
    u32 vr = adc_10q6;
    u32 r2_26q6 = (vr * r1 * 64)/(0xFFC0L - vr);
    
    /* can this overflow here?
     * floating input gives ~155 ohms
     * but 220 ohm input gives 9999 below.
     */
    
    if (r2_26q6 > r2fixed_10q6)
    {
        r2_26q6 -= r2fixed_10q6;
    }
    else
    {
        r2_26q6 = 0;
    }

    output_pressure(r2_26q6>>6, mode, boost_get_psig());
    return 0;
}

#define MAX_OHMS 186
void output_pressure(u8 r, u8 mode, s8 psig)
{

    if (r > MAX_OHMS)
    {
        /* set error flag */
        output_number(mode, 9999, CENTER_E, CENTER_FLASH);     
        return;
    }

    u8 psi=0;
    u8 i;
    u8 x;

    if (r > 10)
        x = r-10;
    else
        x = 0;

    for(i=0; x>=16; i++)
    {
        psi += deltalut(i);
        x -= 16;
    }
    u8 m;
    /* m/16 = slope */
    if (i < 12)
        m = deltalut(i);
    else
        m = deltalut(i-1);

    psi += (m*x + 8)/16;

    u8 f = 0;
    u8 c = 0;

    s16 n;    
    if (is_metric())
    {
        n = ((s16)psi*20)/29;    //  = *10/14.5
        c |= CENTER_B;
    }
    else
    {
        n = (s16)psi;
        f |= PAD_DECIMAL;
    }

    output_number(mode, n, c, f);
}

static inline u8 oilpres_config_func(ui_mode_t mode, ui_display_event_t event)
{
    if (event == CONFIG_SHORT_PRESS)
    {
	    ++oilpres_ctx.dispmode.word;
        save_persist_data(PDATA_OILPRES_UNITS, ~oilpres_ctx.dispmode.word);
    }
    else if (event == CONFIG_LONG_PRESS)
    {
        /* done */
        return 0;
    }

    output_pressure(MAX_OHMS, mode, 0);

    /* not done */
    return 1;   
}
    
