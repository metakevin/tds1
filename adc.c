/******************************************************************************
* File:              adc.c
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

#include <avr/io.h>
#include <avr/interrupt.h>
#include "adc.h"

adc_context_t *context_array;
u8             num_contexts;
u8             next_context;

/******************************************************************************
* adc_init_adc
*       This runs in single conversion mode, not free-running mode.
*       Each ADC context will be programmed to the ADC for sampling in
*       turn.
*       Callbacks each sample.
*******************************************************************************/
void adc_init_adc(u8 prescaler, 
                  u8 nctx,
                  adc_context_t *contexts)
{
    
    num_contexts = nctx;
    if (num_contexts < 1)
        return;
    context_array = contexts;
    
    /* Make sure ADC is disabled */
    ADCSRA = 0;
    
    /* Set ADMUX:
     *  7:6 - REFS1:0 - reference selection */
    /* Set mux to pin of first context */
    ADMUX = 0;
    ADMUX |= (contexts[0].ref << REFS0);        
    ADMUX |= contexts[0].pin;
        
    /* Enable ADC interrupt */
    ADCSRA |= (1<<ADIE);
    
    /* Prescaler */
    ADCSRA &= ~((1<<ADPS0)|(1<<ADPS1)|(1<<ADPS2));
    ADCSRA |= (prescaler<<ADPS0);
        
    /* Clear pending ADC interrupt */
    ADCSRA |= (1<<ADIF);
    
    
    /* Enable ADC */
    ADCSRA |= (1<<ADEN);

    /* Start first conversion */
    ADCSRA |= (1<<ADSC);
}

/* FIXME: Load next ADC command before calling callback.
 * This will increase Fs */
SIGNAL(SIG_ADC)
{
    u8 low;
    u8 high;

    if (context_array[next_context].enabled)
    {
        low = ADCL;
        high = ADCH;
    
        context_array[next_context].sample = (low | (high<<8));
    
        (context_array[next_context].callback)(&context_array[next_context]);
    }

    next_context++;
    if (next_context >= num_contexts)
    {
        next_context = 0;
    }
    
    /* Set up next conversion */
    ADMUX &= ~(0xF|(1<<REFS0)|(1<<REFS1));
    ADMUX |= context_array[next_context].pin;
    ADMUX |= (context_array[next_context].ref << REFS0);
    ADCSRA |= (1<<ADSC);
}
