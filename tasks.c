/******************************************************************************
* File:              tasks.c
* Author:            Kevin Day
* Date:              December, 2004
* Description:       
*                    Mailbox routines.
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

#include "tasks.h"
#include "avrsys.h"
#include "comms_generic.h"

/******************************************************************************
* read_wrap
*        Read byte at 'offset' from box->head.
*******************************************************************************/
static  u8 read_wrap(mailbox_t *box, u8 offset)
{
    if (box->head + offset < box->start + box->size)
    {
        return *(box->head + offset);
    }
    else
    {
        return *(box->head + offset - box->size);        
    }
}

/******************************************************************************
* write_wrap
*        Write byte to box->tail.
*        Increment box->tail.
*******************************************************************************/
static  void write_wrap(mailbox_t *box, u8 val)
{
    *(box->tail) = val;

    ++box->tail;
    
    if (box->tail >= box->start + box->size)
    {        
        box->tail = box->start;
    }
}

/******************************************************************************
* mailbox_deliver
*   Deliver a message to the mailbox.
*   There is a lot of arithmetic here because the compiler and
*   assembler refuse to align mailbox buffers to arbitrary powers
*   of two (which would allow a simple and neat mask-based wrap).
*   On the plus side, this way there is no alignment or size
*   restriction on the mailbox buffer. 
*******************************************************************************/
u8 mailbox_deliver(mailbox_t *box, u8 code, u8 payload_length, u8 *payload)
{
    u8 len, flags, freespace;
    if (payload_length)
    {
        len = payload_length + 2;
        code |= 0x80;
    }
    else
    {
        len = 1;
        code &= ~0x80;
    }

    flags = disable_interrupts();

    if (box->tail >= box->head)
    {        
        freespace = box->size - (box->tail - box->head);
    }
    else
    {
        freespace = box->head - box->tail;
    }

	/* If we write 1 into the last byte and increment tail,
	 * then head will equal tail but the mailbox is actually
	 * full, not empty.  Don't do this. */
        
    /* check for space.  */
    if (freespace < len )//|| freespace == 1)
    {
        restore_flags(flags);
        DEBUG("mailbox_deliver: out of room");
        return 1;   /* no room */
    }
    write_wrap(box, code);
    
    if (payload_length)
    {
        u8 i;
        write_wrap(box, payload_length);
        for(i=0; i<payload_length; i++)
        {
            write_wrap(box, payload[i]);
        }
    }
    restore_flags(flags);
    return 0;
}

/******************************************************************************
* mailbox_head
*      Return the header of the first message in the queue, if it exists.
*      Returns zero iff the mailbox is empty.
*******************************************************************************/
u8   mailbox_head(mailbox_t *box, u8 *code, u8 *payload_len)
{
    if (box->head == box->tail)
    {
        return 0;
    }

    *code = *(box->head);
    if (*code & 0x80)
    {
        *payload_len = read_wrap(box, 1);
        *code &= ~0x80;
    }
    else
    {
        *payload_len = 0;
    }
    return 1;
}
    
/******************************************************************************
* mailbox_copy_payload
*        This function REQUIRES that the head pointer point to the beginning
*        of a message with a payload.  It should only be called after
*        mailbox_head returns a payload_len greater than zero.
*******************************************************************************/
u8   mailbox_copy_payload(mailbox_t *box, u8 *buf, u8 buflen, u8 offset)
{
    u8 i, plen;
    plen = read_wrap(box, 1);

    for(i=0; i<buflen; i++)
    {
        if (i == plen)
        {
            break;
        }
        buf[i] = read_wrap(box, 2 + i + offset);
    }
    return i;
}

/******************************************************************************
* mailbox_advance
*        Advance the write pointer.  The payload returned in mailbox_head
*        will be no longer available.
*******************************************************************************/
void mailbox_advance(mailbox_t *box)
{
    if (*(box->head) & 0x80)
    {
        if (box->head + 1 < box->start + box->size)
        {
            box->head += 2 + *(box->head + 1);
        }
        else
        {
            box->head += 2 + *(box->start);
        }
    }
    else
    {
        box->head += 1;
    }
    // was if, just as a recovery mechanism use while
    while (box->head >= box->start + box->size)
    {
        box->head -= box->size;
    }
}

task_t *tasklist[MAX_TASKS];
u8      num_tasks;

task_t *get_task_by_id(u8 task_id)
{
    u8 i;
    for(i=0; i<num_tasks; i++)
    {
        if (tasklist[i]->task_id == task_id)
        {
            return tasklist[i];
        }
    }
    return NULL;
}

u8 send_to_task(u8 taskid, u8 code, u8 payload_len, u8 *payload)
{
    task_t *task = get_task_by_id(taskid);

    if (task)
    {
        return mailbox_deliver(&(task->mailbox), code, payload_len, payload);
    }
    else
    {
        return 2;
    }
}
        
task_t *setup_task(task_t *task, u8 taskid, taskfunc_t tf, u8 *buf, u8 size)
{
    task->mailbox.head    = buf;
    task->mailbox.tail    = buf;
    task->mailbox.start   = buf;
    task->mailbox.size    = size;
    task->task_id         = taskid;
    task->taskfunc        = tf;
    return task;
}
    
