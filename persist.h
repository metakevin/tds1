/******************************************************************************
* File:              persist.h
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

#ifndef PERSIST_H
#define PERSIST_H

#include "types.h"
#include "eeprom.h"

/* Location in EEPROM of start of persistent data */
#define PERSIST_DATA_OFFSET 0

/* The default value for all data is zero.
 * keys not present in eeprom will read as zero.
 * a corrupted eeprom will be erased, and all keys read as zero */

typedef enum {
    PDATA_LAST_MODE,
    PDATA_MAP_UNITS,
    PDATA_ATMOSPHERIC,
    PDATA_ATMOSPHERIC_HIGH8,
    PDATA_IAT_UNITS,
    PDATA_MAP_SENSOR_INDEX,
    PDATA_EGT_UNITS,
    PDATA_CONFIG_WORD_1L,
    PDATA_CONFIG_WORD_1H,
    PDATA_CONFIG_WORD_2L,
    PDATA_CONFIG_WORD_2H,
    PDATA_OILPRES_UNITS,
    PDATA_FUELPRESSURE_UNITS,
    PDATA_CHECKSUM              /* this must be the last entry */
} persist_data_key_t;

void init_persist_data();

static inline u8 load_persist_data(persist_data_key_t key)
{
    return read_eeprom(PERSIST_DATA_OFFSET+key);
}

/* Note: key must be the lesser cardinal value of two enums.
 *       The value in the EEPROM is in little endian format */
static inline u16 load_persist_data_16(persist_data_key_t key)
{
    return  (u16)read_eeprom(PERSIST_DATA_OFFSET+key) |
           ((u16)read_eeprom(PERSIST_DATA_OFFSET+key+1)<<8);
}
    

void save_persist_data(persist_data_key_t key, u8 value);
    
void save_persist_data_16(persist_data_key_t key, u16 value);

void erase_persist_data();

#endif /* !PERSIST_H */
