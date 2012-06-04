/******************************************************************************
* File:              eeprom.c
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
#include "eeprom.h"

void write_eeprom(u16 address, u8 value)
{
    while (EECR & (1<<EEPE))
        ;
    
    EEAR = address;
    EEDR = value;
    
    EECR |= (1<<EEMPE);
    EECR |= (1<<EEPE);
}

u8 read_eeprom(u16 address)
{
    while (EECR & (1<<EEPE))
        ;
    
    EEAR = address;
    
    EECR |= (1<<EERE);

    return EEDR;
}

