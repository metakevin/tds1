/******************************************************************************
* File:              timers.c
* Author:            Kevin Day
* Date:              December, 2004
* Description:       
*                    
*                    
* Copyright (c) 2004 Kevin Day
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
#include "timers.h"
#include "tasks.h"
#include "platform.h"
#include "avrsys.h"


/* All timer functions are run from the system clock.
 * The system clock is 1kHz.
 * 1ms is the smallest event granularity.
 * Every 1ms, the timer 1 count is reset in hardware.
 * This allows more resolution for timing events, even
 * though events can't be scheduled at that resolution.
 * If this timing resolution is necessary any systemic
 * errors will have to be measured for compensation.
 */

timerinterval_t systemTick;
timersubtick_t  systemSubTick;

timerinterval_t readsubtick()
{
    return TCNT1 / ((CPU_FREQ/TICKS_PER_SEC)/SUBTICKS_PER_TICK);
}
    

void systimer_init()
{
    /* Timer 1 values:
     * TCCR1A:
     *          7:6 COM1A1    00  // OC1A disconnected
     *          5:4 COM1B1    00  // OC1B disconnected
     *          3:3 FOC1A     0   // no force
     *          2:2 FOC1B     0   // no force
     *          1:0 WGM1 1:0  00  // CTC (mode 4 & 0x3)
     * TCCR1B:
     *          7:7 ICNC1     0   // no noise canceller
     *          6:6 ICES1     0   // n/a
     *          5:5           0   // reserved
     *          4:3 WGM1 3:2  01  // CTC (mode 4 >> 2)
     *          2:0 CS1       001 // CLKio/1 -- no prescaling
     */
    TCCR1A = 0;
    TCCR1B = 9;
    
    /* Set count to zero */
    TCNT1 = 0;
    /* Set output compare to every 1ms */
    OCR1A = CPU_FREQ/TICKS_PER_SEC;

    /* 
     *          5: TCIE1     0  // input capture
     *          4: OCIE1A    1  // output compare A match enable
     *          3: OCIE1B    0  // output compare B match 
     *          2: TOIE1     0  // overflow 
     */
    TIMSK1 &= ~((1<<ICIE1)|(1<<OCIE1B)|(TOIE1));
    TIMSK1 |= (1<<OCIE1A);
}

timerentry_t *timerChainHead;
SIGNAL(SIG_OUTPUT_COMPARE1A)
{
    ++systemTick;
    timerentry_t **tpp;
    
    for(tpp=&timerChainHead; *tpp; )
    {        
        timerentry_t *tp = *tpp;
        if (tp->downcount == 0)
        {
            /* Remove from list.  It may be re-added by the callback. */
            *tpp = tp->next;
        
            /* Call the callback */
            (tp->callback)(tp);            
        }
        else
        {
            --tp->downcount;
            tpp=&((*tpp)->next);
        }
    }
}

void register_timer_callback(timerentry_t *entry,
                                timerinterval_t ticks_from_now,
                                timercallback_t callback,
                                u16 key)
{
    u8 flags;

    entry->callback = callback;
    entry->key = key;

    flags = disable_interrupts();

    // just in case 
    // could find and change rather than remove
    remove_timer_callback(entry);
    
    if (ticks_from_now == 0)
    {
        ticks_from_now = 1;
    }
    
    entry->downcount = ticks_from_now;
    
    entry->next = timerChainHead;
    timerChainHead = entry;
    
    restore_flags(flags);
}
    
u8 remove_timer_callback(timerentry_t *t)
{
    u8 flags = disable_interrupts();
    timerentry_t **tpp;    
    for(tpp=&timerChainHead; *tpp; )
    {        
        timerentry_t *tp = *tpp;
        if (tp == t)
        {
            /* Remove from list. */
            *tpp = tp->next;        
            restore_flags(flags);
            return 1;
        }
        else
        {
            tpp=&((*tpp)->next);
        }
    }
    restore_flags(flags);
    return 0;
}



