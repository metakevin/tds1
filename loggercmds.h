/******************************************************************************
* File:              loggercmds.h
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

#ifndef LOGGERCMDS_H
#define LOGGERCMDS_H

typedef enum {
    FLASH_CMD_READ_SPCR         = 1,        /* temporary */
    FLASH_CMD_INITIALIZE        = 2,
    FLASH_CMD_READ_RANGE        = 3,
    FLASH_CMD_BEGIN_SAMPLING    = 4,
    FLASH_CMD_LOG_SAMPLE        = 5,
    FLASH_CMD_READ_PAGE         = 6,
    FLASH_CMD_READ_BYTE         = 7,
    FLASH_CMD_ERROR             = 0xF
} flash_cmd_t;

typedef enum {
    FLASH_ERR_BAD_PARAMS = 1,
    FLASH_ERR_TOO_LARGE  = 2,
    FLASH_ERR_BAD_ADDR   = 3,
    FLASH_ERR_BAD_CMD    = 4,
} flash_err_t;

        

#endif /* !LOGGERCMDS_H */
