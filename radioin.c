/******************************************************************************
* File:              radio_in.c
* Author:            Kevin Day
* Date:              March, 2005
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
#include <avr/signal.h>
#include "tasks.h"
#include "timers.h"
#include "userinterface.h"
#include "radioin.h"
#include "udelay.h"
#include "comms_generic.h"
#include "audiradio.h"

/* D3 - interrupt 1 - clock in */
#define CLKIN_PORT PORTD
#define CLKIN_BIT  3
#define CLKIN_DIR  DDRD
#define CLKIN_PIN  PIND

/* C0 - data in */
#define DATA_PIN PINC
#define DATA_DIR DDRC
#define DATA_PORT PORTC
#define DATA_BIT 0


timerentry_t    rin_idle_timer;
u8              falling_edge_seen;
u8              clock_is_idle;

enum {
    VALID_MSG_HIGH_CLOCK,
    WITHIN_20MS_OF_LAST_EDGE,
    CLOCK_LOW_FOR_A_WHILE,
} radio_in_state;

u8 no_signal_counter;

u8 blank_screen_msg[7] = 
    { 0x1a, 0x00, 0x3F, 0x3F, 0x3F, 0x20, 0x30};

u8 rdo_mode_msg[7] = 
    { 0x04, 0x00, 0x3B, 0x3A, 0x30, 0x3F, 0x0F};


static task_t rin_taskinfo;
u8 radio_in_task();

task_t *radio_input_task_create()
{
    CLKIN_DIR  &= ~(1<<CLKIN_BIT);    // input
    //CLKIN_PORT |= (1<<CLKIN_BIT);     // pullup
    CLKIN_PORT &= ~(1<<CLKIN_BIT);     // no pullup
    DATA_DIR   &= ~(1<<DATA_BIT);     // input
    //DATA_PORT  |= (1<<DATA_BIT);      // pullup
    DATA_PORT  &= ~(1<<DATA_BIT);      // no pullup

//    EICRA &= ~(1<<ISC11); -- both edges

    /* Enable interrupt 1 for rising edge */
    EICRA |= (1<<ISC11);
    EICRA |= (1<<ISC10);
    
    EIMSK |= (1<<INT1);     

    register_display_mode(MODE_RADIO, radioin_display_func);

    return setup_task(&rin_taskinfo, TASK_ID_RADIO_INPUT, radio_in_task, 0, 0);
}

void check_for_valid_msg();
u8 is_valid;
u8 last_valid_msg[7];
u8 last_change_msg[7];

u8 received_bits[14];
u8 next_bit;


void antenna_relay(u8 active);

u8 radio_in_task()
{
    check_for_valid_msg();

#if 0
    if (is_valid)
    {
		if (last_valid_msg[1] == 0x08 ||
			last_valid_msg[1] == 0x40)
		{
			/* CDC or TAPE mode.  Disable antenna. */
			antenna_relay(1);
		}
		else
		{
			antenna_relay(0);
		}
	}
#else
    static u8 prev_next;
    if (prev_next != next_bit)
    {
        send_msg(BROADCAST_NODE_ID, 0xdd, 7,
                received_bits);
        prev_next = next_bit;
    }
#endif

    
    return 0;
}

/* Make this really simple.  Avoid using time or the enable
 * signal to determine state.  Simply accumulate bits on 
 * rising clock edges until two identical 7 word messages
 * are received.  
 *  
 *  There should be 56 rising edges per message.
 *  
 *
 *  Bits are stored in the array MSB first
 *  The array is not shifted when bits are received.
 *  It is a circular bit buffer.
 * */


static inline u8 read_clock()
{
    return (CLKIN_PORT&(1<<CLKIN_PIN)) ? 1 : 0;
}


u8 rdo_screen_counter;

u8 radioin_display_func(ui_mode_t mode, ui_display_event_t event)
{
//    u8 dbg[] = {rdo_screen_counter, no_signal_counter, is_valid};
//    send_msg(BROADCAST_NODE_ID, 0x87, sizeof(dbg), dbg);

    if (event >= IN_CONFIG)
        return 0;

    if (event == DISPLAY_MODE_JUST_CHANGED)        
    {
        rdo_screen_counter = 4;
        no_signal_counter = 0;
    }
    if (rdo_screen_counter > 0)
    {
        --rdo_screen_counter;
        /* Display RD0 message indicating this is radio mode */
        send_to_task(TASK_ID_RADIO_OUTPUT, RADIO_MSG_SEND, 7, 
                rdo_mode_msg);
        return 0;
    }

    if (is_valid)
    {
        send_to_task(TASK_ID_RADIO_OUTPUT, RADIO_MSG_SEND, 7, 
                last_valid_msg);

        no_signal_counter = 0;
    }
    else
    {
        if (no_signal_counter < 255)
        {
            ++no_signal_counter;
        }
        
        if (no_signal_counter < 4)
        {
            /* do nothing yet -- may be in-between valid messages */
        }
        else if (no_signal_counter < (8*10))
        {
            /* Display RD0 message indicating this is radio mode */
            send_to_task(TASK_ID_RADIO_OUTPUT, RADIO_MSG_SEND, 7, 
                    rdo_mode_msg);
        }
        else
        {
            /* blank screen */
            send_to_task(TASK_ID_RADIO_OUTPUT, RADIO_MSG_SEND, 7, 
                    blank_screen_msg);
        }
    }
    return 0;
}

u8 isr_check;
#ifdef RDO_DBG
u8 isr_fired;
#endif

void external_interrupt_1()
{    
    falling_edge_seen = 1;

    isr_check = 1;
#ifdef RDO_CHECK
    isr_fired = 1;
#endif
    
    if (clock_is_idle)
    {
    //    next_bit = 0;
        clock_is_idle = 0;
    }
    
    if (DATA_PIN & (1<<DATA_BIT))
    {
        /* zero bit -- the signal is inverted coming in */
        received_bits[next_bit/8] &= ~(1<<(7-next_bit%8));
    }
    else
    {
        /* one bit */
        received_bits[next_bit/8] |= (1<<(7-next_bit%8));
    }
    next_bit = (next_bit+1)%112;

    if(0)
    {
        send_msg_buffered(BROADCAST_NODE_ID, 0xE0, 7,
                received_bits,0);
        return;
    }
        
}

SIGNAL(SIG_INTERRUPT1)
{
    external_interrupt_1();
}

extern u8 radio_change_counter;

void check_for_valid_msg()
{       
    u8 i;
    u8 zeros_ones = 0;
    u8 invalidate = 0;

    isr_check = 0;
    
    /* Now check if this completes a message pair.
     * The message will go from 
     *  First instance:  next_bit+1 ... next_bit+57
     *  Second instance: next_bit+58 .. next_bit
     *  Both modulo 112. 
     *  The sequence '01' must appear in the string for
     *  the message for to be considered valid.  */

    for(i=0; i<56; i++)
    {
        u8 first_idx  = (next_bit+i)  % 112;
        u8 second_idx = (next_bit+i+56) % 112;
        u8 first_bit  = (received_bits[first_idx/8] 
                         & (1<<(7-first_idx%8)));
        u8 second_bit = (received_bits[second_idx/8] 
                         & (1<<(7-second_idx%8)));
    
        if (! ((first_bit && second_bit) || (!first_bit && !second_bit)))
        {
            /* Mismatch. */
            //is_valid = 0;
            invalidate = 1;
            goto done;
        }
        if (zeros_ones == 0 && !first_bit)
        {
            /* found a zero */
            ++zeros_ones;
        }
        else if (zeros_ones == 1 && first_bit)
        {
            /* found a one */
            ++zeros_ones;
        }
    }

    if (zeros_ones != 2)
    {
        /* Message was all zeros or all ones */
        //is_valid = 0;
        invalidate = 1;
        goto done;
    }

    /* Only accept message as valid if the clock in 
     * ISR did not interrupt this call.  */
    if (isr_check == 0)
    {
        for(i=0; i<56; i++)
        {
            u8 idx  = (next_bit+i)  % 112;
            
            if (received_bits[idx/8] & (1<<(7-idx%8)))
            {
                last_valid_msg[i/8] |= (1<<(7-i%8));
            }
            else
            {
                last_valid_msg[i/8] &= ~(1<<(7-i%8));
            }
        }

        is_valid = 1;

        u8 dochg = 0;
        for(i=0; i<7; i++)
        {
            if (last_valid_msg[i] != last_change_msg[i])
            {
                dochg = 1;
            }
            last_change_msg[i] = last_valid_msg[i];
        }
        if (dochg)
        {
            radio_change_counter = 80;
        }

        /* should start a timer here, and only post it
         * as valid if no new messages arrive for 20 ms.
         * this will catch duplicate strings within message
         * sequences which are not valid message pairs... ? */
    }
    else
    {
        //is_valid = 0;
        invalidate = 1;
    }

//#define RDO_DBG 1
done:
#ifdef RDO_DBG
    if (invalidate)
    {
        send_msg(BROADCAST_NODE_ID, 0xE8, 14, received_bits);
        send_msg(BROADCAST_NODE_ID, 0xE9, 7, last_valid_msg);
    }
#endif
    return;
}
    
