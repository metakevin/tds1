/******************************************************************************
* File:              datalogger.h
* Author:            Kevin Day
* Date:              November, 2005
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

#ifndef DATALOGGER_H
#define DATALOGGER_H


#ifdef EMBEDDED
 #include "dataflash.h"
 #include "avrsys.h"
 #include "tasks.h"
#else
 typedef u32 flash_offset_t;
#endif

#include "loggercmds.h"

#define OFFSET_BITS 24
typedef u32 delta_time_t;
typedef u8  log_index_t;

#define MAX_DESCRIPTORS 64

#define DATA_START_OFFSET (MAX_DESCRIPTORS * sizeof(logged_data_descriptor_t))
#define DATA_END_OFFSET 0x7FFFFL

typedef enum {
    DATA_TYPE_MAP_VOLTS       =    1,
    DATA_TYPE_MAP_PSIG        =    2,
    DATA_TYPE_IAT_VOLTS       =    3,
    DATA_TYPE_VOLTS           =    4,
    DATA_TYPE_SPEEDO_MPH      =    5,
    DATA_TYPE_TACH_RPM        =    6,
    DATA_TYPE_WGFV_RAW        =    7,
    DATA_TYPE_EGT_RAW         =    8,
    DATA_TYPE_WB_VOLTS        =    9,
    DATA_TYPE_FP_VOLTS        =   10,
} logged_data_type_t;

typedef struct {
    u8              flags             : 4;        /* 1010 = sample */
    u8              data_length       : 4;        /* in bytes, -1 (1..16 bytes) */
    u8              data_type;                    /* logged_data_type_t */
    delta_time_t    time_delta_usec;              /* Time from last sample */
    /* data follows, 1 to 16 bytes long */
} logged_data_sample_t;

typedef struct {
    u8 flags;                                   /* 0xDD */
    u8 sequence_number;
    flash_offset_t data_start_offset : OFFSET_BITS;
    flash_offset_t data_length       : OFFSET_BITS;
} logged_data_descriptor_t;

#define FLASH_PAGE_SIZE 256
        
  

#ifdef EMBEDDED

#ifdef LOGGING_SUPPORT
task_t *data_logger_task_create();

void datalogger_init();

log_index_t begin_sampling();
void end_sampling();

void log_data_sample(logged_data_type_t type, u8 length, u8 *data);

#else /* !LOGGING_SUPPORT */
#define log_data_sample(x,y,z)
#endif /* !LOGGING_SUPPORT */

#endif /* EMBEDDED */


#endif /* !DATALOGGER_H */
