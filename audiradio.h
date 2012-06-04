/******************************************************************************
* File:              audiradio.h
* Author:            Kevin Day
* Date:              February, 2005
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

#ifndef AUDIRADIO_H
#define AUDIRADIO_H

#include "types.h"
#include "tasks.h"

#define RADIO_MSG_SEND  2

typedef struct {
    u8 octets[7];
} radio_msg_t;

typedef enum {
    OFF,
    IDLE    = 1,
    START   = 2,  
    ENA_R1  = 3,
    ENA_F1  = 4,
    ENA_R2  = 5,
    CLK_F   = 6,
    CLK_R   = 7,
    ENA_F2  = 8,
    ENA_R3  = 9,
    ENA_F3  = 10,
    POST    = 11,
} radio_state_t;
    

typedef struct {
    radio_msg_t     msg;
    unsigned int    bit             : 6;    /* 0 .. 55 */
    radio_state_t   state           : 4;
    unsigned int    repeat          : 1;    
} radio_context_t;

#define RADIO_MSG_BITS 56


u8 radio_output_send_msg(u8 a, u8 b, u8 c, u8 d, u8 e, u8 f, u8 g);
void radio_disable();

//void radio_output_init();

task_t *radio_output_task_create();

#endif /* !AUDIRADIO_H */
