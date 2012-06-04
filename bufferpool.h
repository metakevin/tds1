/******************************************************************************
* File:              bufferpool.h
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

#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

void bufferpool_init();

/* copied from bget.h */
void	brel	    (void *buf);
void   *bget	    (u8 size);

#ifdef EMBEDDED

#define BUFSZ   9
#define BUFF_MAGIC_INUSE    0xA9
#define BUFF_MAGIC_FREE     0x42
typedef struct {
    u8  buf[BUFSZ];
    u8  flags;
} buff_t;

void bufferpool_release(u8 *buf);

u8 *bufferpool_request(u8 size);
#endif


#endif /* !BUFFERPOOL_H */
