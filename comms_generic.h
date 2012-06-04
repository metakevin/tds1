/******************************************************************************
* File:              comms_generic.h
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

#ifndef COMMS_GENERIC_H
#define COMMS_GENERIC_H

#include "types.h"

#define COMMS_MSG_ECHO_REQUEST 0x1
#define COMMS_MSG_ECHO_REPLY   0x2
#define COMMS_MSG_RESET_BOARD  0xB
#define COMMS_MSG_HELLO        0xF
#define COMMS_MSG_BADTASK      0xE

#define BROADCAST_NODE_ID      0xF


#ifndef EMBEDDED
  #define COMMS_DEBUG 1?:printf
#else
  void xxx(char *, ...);
  #define COMMS_DEBUG 1?:xxx
#endif

#define COMM_PREAMBLE   0x7E
#define COMM_ESCAPE     0x7D
#define COMM_ESCXOR     0x20

/* preamble
 * 0x7E ala HDLC 
 * Not part of structure, but required to begin message on line */
//u8      preamble;       
typedef struct {
    /* from
     *  sender's address 
     *  '7' is invalid */
    u8      from        : 4;
    /* to
     *  recipient's address, or 0x7 (broadcast) */
    u8      to          : 4;
} msgaddr_t;

#define MSG_FLAGS_BIGPACKET 1

typedef struct {
    /* flags
     *  not yet defined */
    /* Flags:
     *      xxx0    Small packet (< 16 byte payload)
     *      xxx1    Big packet.  Payload length field
     *              is set to the number of octets in the
     *              start of the payload which hold the
     *              additional size.  (if more than one
     *              octet of size, the octets are in 
     *              little endian order). */
    u8      flags       : 4;
    /* payload_length
     *  in bytes, not inclusive of header or trailer or checksum */
    u8      payload_length : 4;    

    msgaddr_t address;
    
    /* message_code
     *  defined by recipient.  may constitute entire message.   */
    u8      message_code;
} msghdr_t;

/* The header, payload, and trailer are escaped such that 0x7E will never appear.
 * 0x7D is the escape character.
 * 0x7D (0x7D^0x20) --> 0x7D in payload
 * 0x7D (0x7E^0x20) --> 0x7E in payload */

typedef struct {
    /* checksum (required)
     *  includes header and payload
     *  escape sequence for payload is in effect here also
     *  This is an 8 bit fletcher checksum.
     */
    u8      fcsum_A;  
    u8      fcsum_B;  
} msgtrlr_t;

int send_msg(u8 to, u8 code, u8 payload_len, u8 *payload);

typedef struct {
    u8 A;
    u8 B;
} fcsum_t;

fcsum_t send_msghdr(u8 to, u8 code, u16 payload_len);
void send_msgfcs(fcsum_t fcs);


/* Note: must use _buffered version when interrupts are disabled.
 * If the TX queue fills up, send_msg (actually uart_putc) will 
 * busy wait until the UART TX done interrupt fires. 
 * send_msg_buffered will return non-zero if the comms task mailbox
 * fills up, and the message will be discarded.  */
#define MAX_BUFFERED_MSG_SIZE 16
int send_msg_buffered(u8 to, u8 code, u8 payload_len, u8 *payload, u8 do_crc);

u8 rx_notify(u8 data, u8 error_detected);

void tx_csum_and_escape(u8 octet, fcsum_t *cs);

#endif /* !COMMS_GENERIC_H */
