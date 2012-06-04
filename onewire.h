/******************************************************************************
* File:              onewire.h
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


/* To re-enable (!) sleep mode:
 *
 *  s 0xf 0x64 0x31
 *  s 0xf 0x63 0x31 0x20
 *  s 0xf 0x65 0x31
 */


#ifndef ONEWIRE_H
#define ONEWIRE_H

#include "tasks.h"

task_t *onewire_task_create();
u8 ow_task();

u8 ow_2760_write_reg(u8 reg, u8 val);
u8 ow_2760_read_reg(u8 reg, u8 *buf, u8 len);

#define ONEWIRE_CMD_RESET   1
#define ONEWIRE_READ_2760   2
#define ONEWIRE_WRITE_2760  3
#define ONEWIRE_RECALL_2760 4
#define ONEWIRE_COPY_2760   5

#define OW_CMD_READ_ROM         0x33
#define OW_CMD_SKIP_ROM         0xCC

#define OW_2760_READ_REG        0x69
#define OW_2760_WRITE_REG       0x6C
#define OW_2760_COPY_DATA       0x48
#define OW_2760_RECALL_DATA     0xb8
#define OW_2760_REG_PROTECTION  0x00
#define OW_2760_REG_STATUS      0x01
#define OW_2760_REG_SPECIAL     0x08
#define OW_2760_REG_VOLTS_MSB   0x0c
#define OW_2760_REG_VOLTS_LSB   0x0d
#define OW_2760_REG_CURRENT_MSB 0x0e
#define OW_2760_REG_CURRENT_LSB 0x0f
#define OW_2760_REG_TEMP_MSB    0x18
#define OW_2760_REG_TEMP_LSB    0x19



#endif /* !ONEWIRE_H */
