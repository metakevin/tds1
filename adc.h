/******************************************************************************
* File:              adc.h
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

#ifndef ADC_H
#define ADC_H

#include "types.h"

struct _adc_context;
typedef void (*adc_callback_t)(struct _adc_context *);

typedef enum {
    ADC_AVCC=1, 
    ADC_INT_256=3, 
    ADC_AREF=0
} adc_reference_t;

typedef struct _adc_context {
    unsigned int    pin     : 3;
    unsigned int    enabled : 1;
    adc_reference_t ref     : 2;
    unsigned int    sample  : 10;
    adc_callback_t  callback;
} adc_context_t;



typedef enum {
    ADC_DIV2=0, 
    ADC_DIV4=2, 
    ADC_DIV8=3, 
    ADC_DIV16=4,
    ADC_DIV32=5,
    ADC_DIV64=6,
    ADC_DIV128=7
} adc_prescale_t;

void adc_init_adc(u8 prescaler, 
                  u8 num_contexts,
                  adc_context_t *contexts);

#endif /* !ADC_H */
