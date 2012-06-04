/******************************************************************************
* File:              output.c
* Author:            Kevin Day
* Date:              December, 2005
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

#include "tasks.h"
#include "comms_generic.h"
#include "audiradio.h"
#include "output.h"
#include "userinterface.h"

u8 blink_counter;
extern u8 cluster_is_1995;

//#define BLINK_PHASE_ON(x) ((blink_counter&7)<4)   // for 125ms update
#define BLINK_PHASE_ON(x) ((blink_counter&0xF)<8)   // for 60ms update

void output_msg(u8 radio_msg[7])
{
    ++blink_counter;
    
    if (ui_in_config_mode() && BLINK_PHASE_ON(blink_counter))
    {
        if (cluster_is_1995)
        {
            radio_msg[6] = ((radio_msg[6] & 0xF) | 0xE0);   /* U. */
        }
        else
        {
            radio_msg[6] |= 0xF0;   /* U_ */
        }
    }    
    send_to_task(TASK_ID_RADIO_OUTPUT, RADIO_MSG_SEND, 7, radio_msg);
}

void output_number(u8 mode_index, s16 value, center_letter_t center, number_display_flags_t dflags)
{
    u8 radio_msg[7];    

    /* When in config mode, blink the mode number for a visual cue */
    
    if (mode_index < 0x10)      /* 0x0x - U modes */
    {
        radio_msg[0] = 0x1C;
    }
    else if (mode_index < 0x20) /* 0x1x - V modes */
    {
        radio_msg[0] = 0x3C;    /* 'V' */
    }
    else
    {
        radio_msg[0] = 0xA0;    /* 'M' */
    }
        
    radio_msg[1] = 0;
    radio_msg[6] = mode_index<<4; /* only least significant nibble 
                                     of mode_index used here */

    if (!(dflags & (CENTER_FLASH|CENTER_ALT)) || BLINK_PHASE_ON(blink_counter))
    {
        radio_msg[6] |= (center&0xF);
    }
    else if (dflags & CENTER_ALT)
    {
        radio_msg[6] |= (center>>4);
    }

    u8 i;
    
    if (!(dflags & PAD_DECIMAL))
    {
        i = 5;
    }    
    else
    {
        i = 4;
        radio_msg[5] = 0x30;
    }

    if (value < 0)
    {
        if (cluster_is_1995)
        {
            radio_msg[2] = 0x3D;        /* = */
        }
        else
        {
            radio_msg[2] = 0x3E;        /* ) , sort of like '-' */
        }
        value = -value;
    }
    else 
    {
        /* This will be overwritten below if i = 4 */
        radio_msg[2]  = 0x30 + (value%10000)/1000;
    }
    

    radio_msg[i]    = 0x30 + value%10;
    radio_msg[i-1]  = 0x30 + (value%100)/10;
    radio_msg[i-2]  = 0x30 + (value%1000)/100;
   
    if (!BLINK_PHASE_ON(blink_counter))
    {
        u8 i = 5;
        /* dflags: bits 7:4 mask msg octets 5:2 */
        for(i=5; i>=2; i--)
        {
            if (dflags & (1<<(i+2)))
            {
                radio_msg[i] = 0x3E;  /* ok for 95? */
            }
        }
    }
    
    output_msg(radio_msg);
}

    
void output_temperature(u8 mode_index, s16 Tval, temp_format_mode_t mode_flags)
{
    center_letter_t center;
    number_display_flags_t dflags = 0;

    if (mode_flags & TEMP_PEAK_HOLD)
    {
        dflags = CENTER_FLASH;
    }
    
    if (mode_flags & TEMP_SHOW_ERROR)
    {
        center = CENTER_E;
        dflags = CENTER_FLASH;
    }
    else if (mode_flags & TEMP_FAHRENHEIT)
    {
        Tval = (Tval * 9)/5 + 32;
        center = CENTER_F;
    }
    else
    {
        center = CENTER_C;
    }

    if (!(mode_flags & TEMP_RANGE_1000))
    {
//        Tval *= 10;
        dflags |= PAD_DECIMAL;
    }
    
    output_number(mode_index, Tval, center, dflags);
}


