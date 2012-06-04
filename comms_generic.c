/******************************************************************************
* File:              comms_generic.c
* Author:            Kevin Day
* Date:              December, 2004
* Description:       Framed serial protocol, cross-platform subset
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
#include "comms_generic.h"
#include "bufferpool.h"


/* External byte transmit routine.
 * Will block on full buffer.
 */
void tx_enqueue(u8 data);

/* External node ID routine 
 * Return unique 4 bit value
 * '7' is invalid. */
u8 get_node_id();


static void tx_enqueue_with_escape(u8 octet)
{
    if (octet == 0x7D || octet == 0x7E)
    {
        tx_enqueue(0x7D);
        tx_enqueue(octet ^ 0x20);
    }
    else
    {
        tx_enqueue(octet);
    }
}

void tx_csum_and_escape(u8 octet, fcsum_t *cs)
{
    tx_enqueue_with_escape(octet);
    cs->A += octet;
    cs->B += cs->A;
}

#define ETOOBIG 2
#define EBADADDR 3

fcsum_t send_msghdr(u8 to, u8 code, u16 payload_len)
{
    fcsum_t fcsum = {0,0};
    
    /* Build the message up one byte at a time rather than
     * using the structure, to save some RAM. */

    tx_enqueue(COMM_PREAMBLE);

    if (payload_len >= 16)
    {
        /* Set "payload" length to 2 indicating there
         * are two octets of payload size preceeding the
         * payload itself. */
        tx_csum_and_escape(((1<<4) | MSG_FLAGS_BIGPACKET), &fcsum);
    }
    else
    {
        /* flags . payload_length -- flags are 0 */
        tx_csum_and_escape(payload_len<<4, &fcsum);
    }
    
    tx_csum_and_escape(to<<4 | get_node_id(), &fcsum);
    
    tx_csum_and_escape(code, &fcsum);

    if (payload_len >= 16)
    {
        tx_csum_and_escape(payload_len>>8, &fcsum);
        tx_csum_and_escape(payload_len, &fcsum);
    }

    return fcsum;
}

#define SANITY_CHECK
int send_msg(u8 to, u8 code, u8 payload_len, u8 *payload)
{
    s8 i;

    fcsum_t fcsum = send_msghdr(to, code, payload_len);

    for(i=0; i<payload_len; i++)
    {
        u8 v = payload[i];
        tx_csum_and_escape(v, &fcsum);
    }
    
    send_msgfcs(fcsum);

    return 0;
}

void send_msgfcs(fcsum_t fcsum)
{
    tx_enqueue_with_escape(fcsum.A);
    tx_enqueue_with_escape(fcsum.B);
}    

#ifdef PACKET_RECEIVE_SUPPORT 
/* External packet dispatcher.
 * Called when a packet is successfully received. */
void packet_received(msgaddr_t addr, u8 code, u8 length, u8 flags, u8 *payload);
void bad_packet_received(msgaddr_t addr, u8 code, u8 length, u8 flags, u8 *payload);

/* Buffer allocator */
u8 *bufferpool_request(u8 size);
void bufferpool_release(u8 *ptr);

/* Asynchronous notification of byte reception. 
 * Called by serial driver. 
 * On error case (i.e. bad async frame), the current
 * packet (if any) is aborted and hunt mode is entered. */
typedef enum {
    PREAMBLE_HUNT=0,
    IN_HEADER=1,
    IN_PAYLOAD=2,
    IN_TRAILER=3
} rx_state_t;

typedef struct {
    rx_state_t  state;
    u8          esc;
} rx_status_t;

rx_status_t     rx_stat = {PREAMBLE_HUNT, 0};
msghdr_t        rx_hdr_buf;
msgtrlr_t       rx_trlr_buf;
u8              *rx_buf_ptr;
u8              *rx_payload_ptr;
u16             rx_payload_len;

#ifndef EMBEDDED
#define DO_CSUM
#endif

#ifdef DO_CSUM
fcsum_t fcsum_rcv;
#endif

u8  rx_notify(u8 data, u8 error_detected)
{
    u8 ret = 0;
    
    COMMS_DEBUG(" rx_notif(%02X) : state %d esc %d hdr %X payload %X trlr %X ptr %X\n", 
            data,
            rx_stat.state, rx_stat.esc,
            &rx_hdr_buf, rx_payload_ptr, &rx_trlr_buf, rx_buf_ptr);
                
    if (data == COMM_PREAMBLE)
    {
        if (rx_stat.state != PREAMBLE_HUNT)
        {
            COMMS_DEBUG("Unexpected preamble in state %d\n", rx_stat.state);
        }
        if (rx_payload_ptr)
        {
            bufferpool_release(rx_payload_ptr);
        }
        
#ifdef DO_CSUM
        fcsum_rcv.A = fcsum_rcv.B = 0;
#endif
        
        rx_stat.state = IN_HEADER;
        rx_stat.esc = 0;
        rx_buf_ptr = (u8*)&rx_hdr_buf;
        return 0;
    }

    if (rx_stat.esc)
    {
        /* Last character was 0x7D */
        data ^= COMM_ESCXOR;
        rx_stat.esc = 0;
    }
    else if (data == COMM_ESCAPE)
    {
        /* No character available yet */
        rx_stat.esc = 1;
        return 0;
    }

#ifdef DO_CSUM
    if (rx_stat.state != IN_TRAILER)
    {
        fcsum_rcv.A += data;
        fcsum_rcv.B += fcsum_rcv.A;
        COMMS_DEBUG("CSUM %02X: %02X %02X\n", data, fcsum_rcv.A, fcsum_rcv.B);
    }
#endif
    
    switch(rx_stat.state)
    {
        case PREAMBLE_HUNT:
            COMMS_DEBUG("Junk in hunt state: %02X\n", data);
            break;
        case IN_HEADER:
            *rx_buf_ptr++ = data;
            if (rx_buf_ptr == (u8*)&rx_hdr_buf + sizeof(msghdr_t))
            {
                if (rx_hdr_buf.payload_length > 0)
                {
                    /* Payload size is known.  Get a buffer. */
                    rx_payload_ptr = bufferpool_request(rx_hdr_buf.payload_length);
                    if (rx_payload_ptr == 0)
                    {
                        COMMS_DEBUG("Failed to allocate buffer of %d bytes\n",
                                rx_hdr_buf.payload_length);
                        rx_stat.state = PREAMBLE_HUNT;
                        return 0;
                    }
                    rx_stat.state = IN_PAYLOAD;                    
                    rx_buf_ptr = rx_payload_ptr;
                    rx_payload_len = rx_hdr_buf.payload_length;
                }
                else
                {                    
                    rx_stat.state = IN_TRAILER;
                    rx_buf_ptr = (u8*) &rx_trlr_buf;
                }                    
            }
            break;
        case IN_PAYLOAD:
            *rx_buf_ptr++ = data;
            if ((rx_buf_ptr - rx_payload_ptr) == rx_payload_len)
            {
#ifndef EMBEDDED
                /* big packet receive not supported on AVR. */
                if (rx_hdr_buf.flags & MSG_FLAGS_BIGPACKET)
                {
                    u8 i;
                    u32 real_payload_length = 0;
                    for(i=0; i<rx_hdr_buf.payload_length; i++)
                    {
                        real_payload_length += rx_payload_ptr[i]<<(8*i);
                    }
                    bufferpool_release(rx_payload_ptr);
                    rx_payload_ptr = bufferpool_request(real_payload_length);
                    rx_hdr_buf.flags &= ~(MSG_FLAGS_BIGPACKET);
                }
                else
#endif
                {                
                    /* Payload done. */
                    rx_stat.state = IN_TRAILER;
                    rx_buf_ptr = (u8*)&rx_trlr_buf;
                }
            }
            break;
        case IN_TRAILER:
            *rx_buf_ptr++ = data;

            if (rx_buf_ptr == (u8*)&rx_trlr_buf + sizeof(msgtrlr_t))
            {
#ifndef DO_CSUM
                /* Accept packet. 
                 * packet_received must return the buffer
                 * in rx_payload_ptr if it is non-zero length. */
                packet_received(rx_hdr_buf.address, rx_hdr_buf.message_code,
                                rx_hdr_buf.payload_length,
                                rx_hdr_buf.flags,
                                rx_payload_ptr);
                ret = 1;
#else

                if (fcsum_rcv.A != rx_trlr_buf.fcsum_A ||
                    fcsum_rcv.B != rx_trlr_buf.fcsum_B)
                {                        
                    printf("Checksum field %02X %02X - Calculated %02X %02X\n",
                        rx_trlr_buf.fcsum_A, rx_trlr_buf.fcsum_B,
                        fcsum_rcv.A, fcsum_rcv.B);

                    bad_packet_received(rx_hdr_buf.address, rx_hdr_buf.message_code,
                                rx_hdr_buf.payload_length,
                                rx_hdr_buf.flags,
                                rx_payload_ptr);
                    
                }
                else
                {
                    packet_received(rx_hdr_buf.address, rx_hdr_buf.message_code,
                                rx_hdr_buf.payload_length,
                                rx_hdr_buf.flags,
                                rx_payload_ptr);
                    ret = 1;
                }
#endif
                rx_stat.state = PREAMBLE_HUNT;
                rx_payload_ptr = 0;
            }
            break;
        default:
            COMMS_DEBUG("Bad state %d\n", rx_stat.state);
    }
    return ret;
}

#endif // PACKET_RECEIVE_SUPPORT

