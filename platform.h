/******************************************************************************
* File:              platform.h
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

#ifndef PLATFORM_H
#define PLATFORM_H

#define CPU_FREQ 18432000UL
//#define UART_BAUD_RATE 115200L
#define UART_BAUD_RATE 38400L
#define SRAM_END (1024-1)

#define LED_PORT PORTC
#define LED_DIR  DDRC
#define LED_PIN  PINC
#define LED_BIT  5



#endif /* !PLATFORM_H */
