/******************************************************************************
* File:              onewire.c
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

#include "avrsys.h"
#include "timers.h"
#include "platform.h"
#include "tasks.h"
#include "onewire.h"
#include "comms_generic.h"

#define F_CPU  CPU_FREQ
#include <util/delay.h>

//#define smart_delay_us(x) delay_subticks((x)/10)

//#define smart_delay_us(x) 

/* At 18.432 MHz, 18.432 instructions are executed per microsecond.
 * Each iteration is 4 instructions.  So the count parameter should
 * be 18.432/4 * 'us', or 4.6. */
#define delay_us(x) _delay_loop_2( 4.6*x )
//#define smart_delay_us(x) _delay_loop_2( 4.6*x )
//#define smart_delay_us(x) _delay_loop_2( 4.6*x*1.8 )

#ifdef REVB_PROTO
#define OW_FET_BIT  4
#define OW_FET_PORT PORTC
#define OW_FET_DIR  DDRC
#define OW_IN_BIT   0
#define OW_IN_PIN   PINB
#define OW_IN_DIR   DDRB
#define OW_IN_PORT  PORTB
#else
#define OW_FET_BIT  2
#define OW_FET_PORT PORTB
#define OW_FET_DIR  DDRB
#define OW_IN_BIT   0
#define OW_IN_PIN   PINB
#define OW_IN_DIR   DDRB
#define OW_IN_PORT  PORTB
#endif

#if 0
void ow_restore_flags(u8 f)
{
    u8 missed = 0;
    if (EIFR & _BV(INTF1))
    {
        missed = 1;
    }
    restore_flags(f);

    if (missed)
    {
        send_msg(BROADCAST_NODE_ID, 0x7A, 0, NULL);
    }
}
#else
#define ow_restore_flags(x) restore_flags(x)
#endif
#define ow_disable_interrupts disable_interrupts

#if 0
void smart_delay_xx(u16 us)
{
    if (check_interrupt_enable())
    {
        goto done;
    }

    while(1)
    {
        if (us >= 230)
        {
            us -= 230;
            _delay_loop_2(184);
#if 1
            if (EIFR & _BV(INTF1))
            {
                external_interrupt_1();
            }
            else
#endif
            {
                _delay_loop_2(46); //???
            }
        }
        else
        {
done:
            _delay_loop_2(us);
            return;
        }
    }
}
#define smart_delay_us(x) smart_delay_xx(4.6*x)

#else
#define smart_delay_us(x) _delay_loop_2( 4.6*x )
#endif

                


static inline void init_onewire()
{
    OW_FET_PORT &= ~(1<<OW_FET_BIT);    /* FET off */
    OW_FET_DIR  |= (1<<OW_FET_BIT);     /* output */

    OW_IN_DIR &= ~(1<<OW_IN_BIT);       /* input */
    OW_IN_PORT &= ~(1<<OW_IN_BIT);       /* no pullup */
}

static inline void ow_assert_low()
{
    OW_FET_PORT |= (1<<OW_FET_BIT);     /* pin high, fet pulls low */
}

static inline void ow_bus_float()
{
    OW_FET_PORT &= ~(1<<OW_FET_BIT);    /* pin low, turn off fet */
}

static inline unsigned char ow_read()
{
    return (OW_IN_PIN & (1<<OW_IN_BIT))?1:0;
}

u8 ow_reset()
{
    u8 flag;
    u8 result = 0;
    
    init_onewire();
    
    ow_assert_low();

    smart_delay_us(480);
    
    flag = ow_disable_interrupts();
    
    ow_bus_float();

    smart_delay_us(66);
    
    if (ow_read())
    {
        /* No presence pulse */
        result = 1;
    }
 
    smart_delay_us(414);
    if (result==0 && ow_read() == 0)
    {
        /* bus is shorted */
        result = 2; 
    }

        
    ow_restore_flags(flag);


    return result;
}

u8 ow_bit_io(u8 b)
{
    u8 flags;

    flags = ow_disable_interrupts();
    
    ow_assert_low();

    smart_delay_us(1);

    if (b)
    {
        ow_bus_float();
    }

    smart_delay_us(14);

    b = ow_read();

    smart_delay_us(45);

    ow_bus_float();
    
    ow_restore_flags(flags);

    return b;
}

u8 ow_byte_write(u8 b)
{
    u8 i;
    for(i=8; i; i--)
    {
        u8 j;
        j = ow_bit_io(b&1);
        smart_delay_us(5);
        b >>= 1;
        if (j)
        {
            b |= 0x80;
        }
    }
    return b;
}
    

u8 ow_2760_read_reg(u8 reg, u8 *buf, u8 len)
{
    u8 ret;
    ret = ow_reset();
    if (ret)
    {
        return ret;
    }
    
    ow_byte_write(OW_CMD_SKIP_ROM);
    ow_byte_write(OW_2760_READ_REG);
    ow_byte_write(reg);
    while(len--)
    {
        *buf++ = ow_byte_write(0xFF);
    }
    return 0;
}


u8 ow_2760_write_reg(u8 reg, u8 val)
{
    u8 ret;
    ret = ow_reset();
    if (!ret)
    {
        ow_byte_write(OW_CMD_SKIP_ROM);
        ow_byte_write(OW_2760_WRITE_REG);
        ow_byte_write(reg);
        ow_byte_write(val);
    }

#if 0
    {
        u8 msg[] = {reg, val, ret};
        send_msg(BROADCAST_NODE_ID, 0xCA, 3, &msg);
    }
#endif

    return ret;
}

u8 ow_2760_recall_data(u8 addr)
{
    u8 ret;
    ret = ow_reset();
    if (ret)
    {
        return ret;
    }
    
    ow_byte_write(OW_CMD_SKIP_ROM);
    ow_byte_write(OW_2760_RECALL_DATA);
    ow_byte_write(addr);

    return 0;
}

u8 ow_2760_copy_data(u8 addr)
{
    u8 ret;
    ret = ow_reset();
    if (ret)
    {
        return ret;
    }
    
    ow_byte_write(OW_CMD_SKIP_ROM);
    ow_byte_write(OW_2760_COPY_DATA);
    ow_byte_write(addr);

    return 0;
}

        
u8 ow_read_rom(u8 *rombuf)
{
    u8 ret;
    ret = ow_reset();
    if (ret)
    {
        return ret;
    }
    
    ow_byte_write(OW_CMD_READ_ROM);
    {
        u8 i;
        for(i=0; i<8; i++)
        {
            rombuf[i] = ow_byte_write(0xFF);
        }
    }
    return 0;
}


#ifdef ONEWIRE_TASK
task_t ow_taskinfo;
static u8 ow_mailbox_buf[20];

u8 ow_task();

task_t *onewire_task_create()
{
    return setup_task(&ow_taskinfo, TASK_ID_ONEWIRE, ow_task, ow_mailbox_buf, sizeof(ow_mailbox_buf));
}

u8 ow_task()
{
    u8 payload_len, code;

    if (mailbox_head(&ow_taskinfo.mailbox, &code, &payload_len))
    {
        switch(code)
        {
            case ONEWIRE_CMD_RESET:
                {
                    u8 rombuf[8];
                    u8 result;
                    if (payload_len == 1)
                    {
                        u8 flags = ow_disable_interrupts();
                        result = ow_read_rom(rombuf);
                        ow_restore_flags(flags);
                    }
                    else
                    {
                        result = ow_read_rom(rombuf);
                    }
                    if (result)
                    {
                        send_msg(BROADCAST_NODE_ID, TASK_ID_ONEWIRE<<4|ONEWIRE_CMD_RESET, 
                            1, &result);
                    }
                    else
                    {
                        send_msg(BROADCAST_NODE_ID, TASK_ID_ONEWIRE<<4|ONEWIRE_CMD_RESET, 
                            8, rombuf);
                    }
                }
                break;
            case ONEWIRE_READ_2760:
                {
                    u8 buf[15];
                    u8 buflen=0;
                    u8 result;
                    if (payload_len == 2)
                    {
                        mailbox_copy_payload(&ow_taskinfo.mailbox, buf, 2, 0);
                        buflen=buf[1];
                        result = ow_2760_read_reg(buf[0], buf, buflen);
                    }
                    else
                    {
                        result = 0xEE;
                    }
                    
                    if (result)
                    {
                        send_msg(BROADCAST_NODE_ID, TASK_ID_ONEWIRE<<4|ONEWIRE_READ_2760, 
                            1, &result);
                    }
                    else
                    {
                        send_msg(BROADCAST_NODE_ID, TASK_ID_ONEWIRE<<4|ONEWIRE_READ_2760, 
                            buflen, buf);
                    }
                }
                break;
            case ONEWIRE_WRITE_2760:
                {
                    u8 buf[2];
                    u8 result;
                    if (payload_len == 2)
                    {
                        mailbox_copy_payload(&ow_taskinfo.mailbox, buf, 2, 0);
                        result = ow_2760_write_reg(buf[0], buf[1]);
                    }
                    else if (payload_len == 3)
                    {
                        u8 flag = ow_disable_interrupts();
                        mailbox_copy_payload(&ow_taskinfo.mailbox, buf, 2, 0);
                        result = ow_2760_write_reg(buf[0], buf[1]);
                        ow_restore_flags(flag);
                    }
                    else
                    {
                        result = 0xEE;
                    }
                    
                    send_msg(BROADCAST_NODE_ID, TASK_ID_ONEWIRE<<4|ONEWIRE_WRITE_2760, 
                        1, &result);
                }
                break;
            case ONEWIRE_RECALL_2760:
                {
                    u8 buf[1];
                    u8 result;
                    if (payload_len == 1)
                    {
                        mailbox_copy_payload(&ow_taskinfo.mailbox, buf, 1, 0);
                        result = ow_2760_recall_data(buf[0]);
                    }
                    else
                    {
                        result = 0xEE;
                    }
                    
                    send_msg(BROADCAST_NODE_ID, TASK_ID_ONEWIRE<<4|ONEWIRE_RECALL_2760, 
                        1, &result);
                }
                break;
            case ONEWIRE_COPY_2760:
                {
                    u8 buf[1];
                    u8 result;
                    if (payload_len == 1)
                    {
                        mailbox_copy_payload(&ow_taskinfo.mailbox, buf, 1, 0);
                        result = ow_2760_copy_data(buf[0]);
                    }
                    else
                    {
                        result = 0xEE;
                    }
                    
                    send_msg(BROADCAST_NODE_ID, TASK_ID_ONEWIRE<<4|ONEWIRE_COPY_2760, 
                        1, &result);
                }
                break;
        }
        mailbox_advance(&ow_taskinfo.mailbox);
    }
    return 0;
}
#endif


