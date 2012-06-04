/******************************************************************************
* File:              egt.h
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

#ifndef EGT_H
#define EGT_H

void egt_init();

typedef struct {
    u8 cold_junction_temp_msb_lsb[2];
    u8 thermocouple_volts_msb_lsb[2];
} thermocouple_raw_data_t;

extern thermocouple_raw_data_t tc_data;
extern u8                      tc_status;

void egt_read_thermocouple();


#endif /* !EGT_H */
