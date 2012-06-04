/******************************************************************************
* File:              output.h
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

#ifndef OUTPUT_H
#define OUTPUT_H

typedef enum {
    DFLAGS_NONE         = 0,
    CENTER_FLASH        = 1,
    PAD_DECIMAL         = 2,        /* e.g. IAT, with .0 appended */
    CENTER_ALT          = 4,        /* alternate high and low nibbles of center */
    DFLAGS_FLASH_PLACE_0= 16,       /* This must be 16 (see output.c) */
    DFLAGS_FLASH_PLACE_1= 32,
    DFLAGS_FLASH_PLACE_2= 64,
    DFLAGS_FLASH_PLACE_3= 128,
} number_display_flags_t;

typedef enum {
    CENTER_NONE      = 0,
    CENTER_A         = 1,  
    CENTER_B         = 2, 
    CENTER_C         = 3, 
    CENTER_D         = 4, 
    CENTER_E         = 5,
    CENTER_F         = 6,
    ALT_CENTER_NONE  = 0x00,
    ALT_CENTER_A     = 0x10,  
    ALT_CENTER_B     = 0x20, 
    ALT_CENTER_C     = 0x30, 
    ALT_CENTER_D     = 0x40, 
    ALT_CENTER_E     = 0x50,
    ALT_CENTER_F     = 0x60,
} center_letter_t;

void output_number(u8 mode_index, s16 value, center_letter_t center, number_display_flags_t dflags);
    

typedef enum {
    TEMP_NORMAL     = 0, 
    TEMP_PEAK_HOLD  = 1, 
    TEMP_SHOW_ERROR = 2,
    TEMP_RANGE_1000 = 4,
    TEMP_FAHRENHEIT = 8,
} temp_format_mode_t;

void output_temperature(u8 mode_index, s16 Tval, temp_format_mode_t mode_flags);



#endif /* !OUTPUT_H */
