/******************************************************************************
* File:              comms_avr.c
* Author:            Kevin Day
* Date:              December, 2004
* Description:       Framed serial protocol, AVR side
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

#include <avr/wdt.h>
#include "platform.h"
#include "comms_generic.h"
#include "tasks.h"
#include "avrsys.h"
#include "bufferpool.h"

#define COMMS_MSG_HEADER    0x10
#define COMMS_MSG_PAYLOAD   0x11

u8 get_node_id()
{
    return 1;
}

static task_t   comms_taskinfo;
#ifdef COMMS_MAILBOX
static u8       comms_mailbox_buf[20];
#else
static u8       comms_mailbox_buf[1]; // fixme: not needed at all
#endif
u8 comms_task();

/* called by main */
task_t* comms_task_create()
{
    /* Configure the baud rate.
     * Should move this to comms_avr.c */
	UBRR0L = (uint8_t)(CPU_FREQ/(UART_BAUD_RATE*16L)-1);
	UBRR0H = (CPU_FREQ/(UART_BAUD_RATE*16L)-1) >> 8;
	UCSR0A = 0x0;
	UCSR0C = _BV(UCSZ01)|_BV(UCSZ00);
	UCSR0B = _BV(TXEN0)|_BV(RXEN0)|_BV(RXCIE0);
    
    return setup_task(&comms_taskinfo, TASK_ID_COMMS, comms_task, 
            comms_mailbox_buf, sizeof(comms_mailbox_buf));
}

u8 rxfifo_head, rxfifo_tail;
#define RXFIFO_MASK 0xF
u8 rxfifo[RXFIFO_MASK+1];
#if 0
u16 bytes_received;
u8 rx_errors, latch_error;
#endif
SIGNAL(SIG_USART_RECV)
{
    while (UCSR0A & _BV(RXC0))
    {
#if 0
        u8 stat = UCSR0A;
        if (stat&0x18)
        {
            ++rx_errors;
            latch_error = stat;
        }
        ++bytes_received;
#endif
        rxfifo[rxfifo_tail] = UDR0;
        rxfifo_tail = ((rxfifo_tail+1)&RXFIFO_MASK);
    }
}	

/* called by task_dispatcher periodically.
 * returns 1 if work was done that may require
 * a reschedule */
u8 comms_task()
{
#ifdef PACKET_RECEIVE_SUPPORT
    u8 ret = 0;
    u8 code, payload_len;

    while(rxfifo_head != rxfifo_tail)
    {
        rx_notify(rxfifo[rxfifo_head], 0);
        rxfifo_head = ((rxfifo_head+1)&RXFIFO_MASK);
    }

#ifdef COMMS_MAILBOX
    if (mailbox_head(&comms_taskinfo.mailbox, &code, &payload_len))
    {
        static u8 saved_code, saved_to;
        switch(code)
        {
            case COMMS_MSG_HEADER:
                if (payload_len == 2)
                {
                    mailbox_copy_payload(&comms_taskinfo.mailbox,
                            &saved_to, 1, 0);
                    mailbox_copy_payload(&comms_taskinfo.mailbox,
                            &saved_code, 1, 1);
                }
                break;
            case COMMS_MSG_PAYLOAD:
            {
                u8 msgbuf[MAX_BUFFERED_MSG_SIZE];
                u8 sz;
                sz = mailbox_copy_payload(&comms_taskinfo.mailbox,
                    msgbuf, MAX_BUFFERED_MSG_SIZE, 0);
                send_msg(saved_to, saved_code, sz, msgbuf);
                break;
            }
        }
        mailbox_advance(&comms_taskinfo.mailbox);
    }
#endif
    
    return ret;
#else
    return 0;
#endif
}

#ifdef PACKET_RECEIVE_SUPPORT
//u8 packet_drops_overflow;
//u8 packet_drops_badcode;

void packet_received(msgaddr_t addr, u8 code, u8 length, u8 flags, u8 *payload)
{
    /* code is divided into two nibbles.
     * the first nibble indicates the task mailbox for the
     * message, and the second is determined by the task. */
    u8 taskid = code>>4;
    code &= 0xF;
    task_t *task = get_task_by_id(taskid);

#if 0
    u8 dbg[] = {bytes_received>>8, bytes_received, rx_errors, latch_error};
    send_msg(0xF, 0xF3, sizeof(dbg), dbg);
#endif

    if (taskid == TASK_ID_COMMS)
    {
        if (code == COMMS_MSG_ECHO_REQUEST)
        {
            send_msg(addr.from, 
                    TASK_ID_COMMS<<4|COMMS_MSG_ECHO_REPLY, 
                    length, payload);

#if 0
			if (length == 1)
			{
				if (payload[0])
				{
					antenna_relay(1);
				}
				else
				{
					antenna_relay(0);
				}
			}
#endif
        }
#if 0
        else if (code == COMMS_MSG_RESET_BOARD)
        {
            if (length == 4 && 
                    payload[0] == 0xd0 &&
                    payload[1] == 0xd0 &&
                    payload[2] == 0xda &&
                    payload[3] == 0xda)
            {
                /* Change the timeout to 16ms for a fast recycle */
                WDTCSR = _BV(WDE)|_BV(WDCE);
    
                /* Wait for the watchdog to reset the chip */
                while(1)
                    ;
            }
        }
#endif
    }
    else if (task == NULL)
    {
        DEBUG("invalid message code");
//        send_msg(addr.from, 
//                TASK_ID_COMMS<<4|COMMS_MSG_BADTASK, 
//                1, &taskid);
//        ++packet_drops_badcode;
    }
    else 
    {
     if (mailbox_deliver(&(task->mailbox), 
                        code & 0x0F, length, payload))
        {
               DEBUG("mailbox full");
//            ++packet_drops_overflow;
        }
    }    
    if(payload)
    {
        bufferpool_release(payload);
    }
}
#endif // PACKET_RECEIVE_SUPPORT

/* This should be changed to be interrupt-driven.
 * A simple Tx ring queue using the TX done interrupt
 * would suffice... */
void tx_enqueue(u8 data)
{
    while (!(UCSR0A & _BV(UDRE0)))
        ;
    UDR0 = data;
}

#if 0
int send_msg_buffered(u8 to, u8 code, u8 payload_len, u8 *payload, u8 do_crc)
{
    u8 flags;
    u8 header[2];
    u8 ret;
    header[0] = to;
    header[1] = code;

    /* Must disable interrupts to assure that both header and payload
     * arrive in comms mailbox sequentially. */
    ret = 1;
    flags = disable_interrupts();
    if (mailbox_deliver(&comms_taskinfo.mailbox, COMMS_MSG_HEADER, 2, (u8 *)header))
    {
    }
    else if (mailbox_deliver(&comms_taskinfo.mailbox, COMMS_MSG_PAYLOAD, payload_len, payload))
    {
    }
    else
    {
        ret = 0;
    }
    restore_flags(flags);
    return ret;
}
#endif

