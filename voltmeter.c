/******************************************************************************
* File:              voltmeter.c
* Author:            Kevin Day
* Date:              February, 2005
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

#include "adc.h"
#include "timers.h"
#include "comms_generic.h"
#include "tasks.h"
#include "audiradio.h"
#include "userinterface.h"
#include "output.h"
#include "datalogger.h"

#define VOLTMETER_PIN 7

/* The production TDS A uses a 1k/3k voltage divider on the power input.
 * Full scale is 20.0 volts. */

#define CODE_1VOLT 31
#define MAXVOLTS_10Q10 34253  /* 33.45 * 1024 */
#define MAXVOLTS_10Q6  2141   /* 33.45 * 64 */

#define CODE_20V            1023
#define DIV_1VOLT_10Q6      3273       //((CODE_20V*64)/20)  // 3273
#define DIV_1VOLT_10Q4      818        // CODE_20V*16/20
#define DIV_1VOLT_10Q0      51         // COLD_20V/20
#define DIV_1VOLT_10Q10      ((CODE_20V*1024)/20)


void voltmeter_adc_callback(adc_context_t *ctx);
u8 voltmeter_display_func(ui_mode_t mode, ui_display_event_t event);

void voltmeter_init(adc_context_t *adc_context)
{
    adc_context->pin      = VOLTMETER_PIN;
    adc_context->ref      = ADC_AVCC;
    adc_context->enabled  = 1;
    adc_context->callback = voltmeter_adc_callback;

    register_display_mode(MODE_VOLTMETER, voltmeter_display_func);
}

typedef struct {
    unsigned long sample_accumulator    : 16;
    unsigned long current_volts_accum   : 16;
    unsigned long volts_valid           : 1;
    unsigned long sample_counter        : 6;
} vm_ctx_t;
vm_ctx_t vm_ctx;

void voltmeter_adc_callback(adc_context_t *ctx)
{
    vm_ctx.sample_accumulator += ctx->sample;
    ++vm_ctx.sample_counter;
    if (vm_ctx.sample_counter == 0)
    {
        vm_ctx.current_volts_accum = vm_ctx.sample_accumulator;

        u16 v = vm_ctx.current_volts_accum;
        log_data_sample(DATA_TYPE_VOLTS, 2, (u8*)&v);

        vm_ctx.volts_valid = 1;
        vm_ctx.sample_accumulator = 0;        
    }
}

u8 voltmeter_display_func(ui_mode_t mode, ui_display_event_t event)
{
    if (vm_ctx.volts_valid)
    {
        u16 volts_10q6 = vm_ctx.current_volts_accum / DIV_1VOLT_10Q0;
        u16 centivolts = (volts_10q6*10)>>6;
        output_number(mode, centivolts, CENTER_NONE, DFLAGS_NONE);
    }
    return 0;
}




#define VM_11_5_volts 0x92a0   // 51*11.5*64
#define VM_12_5_volts 0x9f60   // 51*12.5*64
#define VM_12_7_volts 0xa1ec
#define VM_12_9_volts 0xa479
#define VM_11_0_volts 0x8c40
u8 voltmeter_out_of_range_11_5_to_12_5()
{
    if (vm_ctx.volts_valid)
    {
        if (vm_ctx.current_volts_accum > VM_12_9_volts)
        {
            return 1;
        }
        /* 11.5 * 64 = 736 */
        if (vm_ctx.current_volts_accum < VM_11_0_volts)
        {
            return 1;
        }
    }
    return 0;
}

