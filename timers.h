/******************************************************************************
* File:              timers.h
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

#ifndef TIMERS_H
#define TIMERS_H

#include "types.h"

#define TICKS_PER_SEC 1000L
#define SUBTICKS_PER_TICK 1000L 
#define MS_TO_TICK(x) ((x)*(TICKS_PER_SEC/1000L))
#define SUBTICK_TO_USEC(x) ((x)*(1000000L/(TICKS_PER_SEC*SUBTICKS_PER_TICK)))
#define TICK_TO_MSEC(x) ((x)*(TICKS_PER_SEC/1000L))

void systimer_init();

typedef u16 timerinterval_t;   /* in milliseconds */

typedef u8 timersubtick_t;    /* in 10s of microseconds (max 99) */

typedef struct {
    timerinterval_t ms;
    timersubtick_t  us_x10;
} precision_time_t;

struct _timerentry;
typedef void (*timercallback_t)(struct _timerentry *);


typedef struct _timerentry {
    timerinterval_t     downcount;
    timercallback_t     callback;
    u16                 key;
    struct _timerentry *next;
} timerentry_t;

/* The caller manages the storage for the timerentry_t structure.
 * When the timer expires, the entry will be passed to the callback.
 * At that point it can be freed or re-registered.
 * The empty_entry parameter doesn't need to be set up. */
void register_timer_callback(timerentry_t *empty_entry,
                                timerinterval_t ticks_from_now,
                                timercallback_t callback,
                                u16 key);


u8 remove_timer_callback(timerentry_t *t);

     
extern timerinterval_t systemTick;

static inline timerinterval_t readtime()
{
    return systemTick;
}

timerinterval_t readsubtick();

void delta_usec(timerinterval_t tick1, timersubtick_t subtick1,
                timerinterval_t tick2, timersubtick_t subtick2,
                timerinterval_t *tickres, timersubtick_t *subtickres);

#endif /* !TIMERS_H */
