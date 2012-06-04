/******************************************************************************
* File:              avrsys.h
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

#ifndef AVRSYS_H
#define AVRSYS_H

#include <avr/interrupt.h>
#include "types.h"

#define DEBUG(str)

static inline u8 check_interrupt_enable()
{
    u8 i_bit = 1;
    
    __asm__ volatile ("brbs 7, 1f\n"
            "andi %0, 0\n"
            "1:" : "+d" (i_bit));

    return i_bit;
}

/******************************************************************************
* disable_interrupts
*        Disable all interrupts by clearing the I bit in SREG.
*        If interrupts were previously enabled, return 1.  Else 0.
*******************************************************************************/
static inline u8 disable_interrupts()
{
//    uint8_t i_bit = inb(SREG) & 0x80;
//    cli();

//    return i_bit;

    u8 i_bit = 1;
    
    __asm__ volatile ("brbs 7, 1f\n"
            "andi %0, 0\n"
            "1:" : "+d" (i_bit));

    cli();

    return i_bit;
}   


static inline void restore_flags(u8 i_bit)
{
    if (i_bit)
    {
        // was enabled; re-enable
        sei();
    }
    // else leave disabled
}


#endif /* !AVRSYS_H */
