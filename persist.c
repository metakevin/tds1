/******************************************************************************
* File:              persist.c
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

#include "types.h"
#include "persist.h"
#include "eeprom.h"

/* Verify checksum.  If invalid, initialize all locations to zero. */
void init_persist_data()
{        
#ifdef PDATA_CHECKSUM
    u8 i, csum;
    
    /* The checksum is stored in negative form.
     * thus the sum of 0 .. PDATA_CHECKSUM should
     * be zero.  This allows an all-zero EEPROM to be valid.
     * That is fine, since EEPROM values are defined with
     * a default value of zero. */
    csum = 0;
    for (i=0; i<=PDATA_CHECKSUM; i++)
    {
        csum += read_eeprom(PERSIST_DATA_OFFSET+i);
    }

    if (csum != 0)
    {
        for(i=0; i<=PDATA_CHECKSUM; i++)
        {
            write_eeprom(PERSIST_DATA_OFFSET+i, 0);
        }
    }
#endif
}

#if 0
void recompute_checksum()
{
    u8 i, csum;
    /* This could be optimized to adjust the checksum
     * based on the difference of the previous and current
     * values of the byte being written. */
    csum = 0;
    for (i=0; i<PDATA_CHECKSUM; i++)
    {
        csum += read_eeprom(PERSIST_DATA_OFFSET+i);
    }
    write_eeprom(PERSIST_DATA_OFFSET+PDATA_CHECKSUM, 0xFF - csum);
}
#endif

void save_persist_data(persist_data_key_t key, u8 value)
{   
    write_eeprom(PERSIST_DATA_OFFSET+key, value);
    
#ifdef PDATA_CHECKSUM
    recompute_checksum();
#endif
}

void save_persist_data_16(persist_data_key_t key, u16 value)
{   
    write_eeprom(PERSIST_DATA_OFFSET+key, value);
    write_eeprom(PERSIST_DATA_OFFSET+key+1, value>>8);
    
#ifdef PDATA_CHECKSUM
    recompute_checksum();
#endif
}

void erase_persist_data()
{
    u8 i;
    
    for (i=0; i<PDATA_CHECKSUM; i++)
    {
        save_persist_data(i, 0xFF);
    }
}


