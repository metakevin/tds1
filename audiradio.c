/******************************************************************************
* File:              audiradio.c
* Author:            Kevin Day
* Date:              February, 2005
* Description:       
*                    Audi radio protocol, transmit side.
*                    Should put receive side here as well, or rename.
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

#include <avr/io.h>
#include <avr/interrupt.h>
#include "audiradio.h"
#include "types.h"
#include "platform.h"
#include "avrsys.h"
#include "comms_generic.h"
#include "tasks.h"
#include "egt.h"

/******************************************************************************
*   Pin definitions
*        
*******************************************************************************/
#define DATAOUT_PORT    PORTD
#define DATAOUT_DIR     DDRD
#define DATAOUT_BIT     7       // D7
#define CLOCKOUT_PORT   PORTD
#define CLOCKOUT_DIR    DDRD
#define CLOCKOUT_BIT    5       // D5
#define ENAOUT_PORT     PORTD
#define ENAOUT_DIR      DDRD
#define ENAOUT_BIT      6       // D6


/******************************************************************************
*   Time constants
*        CPU frequency dependent
*******************************************************************************/
//#define T2_TICKS_65US (65*((CPU_FREQ/8)/1000000.0))
//#define T2_TICKS_40US (40*((CPU_FREQ/8)/1000000.0))

#if CPUFREQ_14MHZ
 #define T2_TICKS_65US  120
 #define T2_TICKS_40US   74
 #define T2_TICKS_60US  111
 #define T2_TICKS_100US 184
#else
 #define T2_TICKS_65US  150
 #define T2_TICKS_40US   92
 #define T2_TICKS_60US  138
 #define T2_TICKS_100US 230
#endif



radio_context_t radio_context;
void radio_state_machine();
            

/******************************************************************************
* set_data_output
*        Note inverted sense!
*******************************************************************************/
static inline void set_data_output(u8 level)
{
    if(!level)
    {
        DATAOUT_PORT |= (1<<DATAOUT_BIT);
    }
    else
    {
        DATAOUT_PORT &= ~(1<<DATAOUT_BIT);
    }
}

/******************************************************************************
* set_clock_output
*        0V = 0, 5V = 1
*******************************************************************************/
static inline void set_clock_output(u8 level)
{
    if(level)
    {
        CLOCKOUT_PORT |= (1<<CLOCKOUT_BIT);
    }
    else
    {
        CLOCKOUT_PORT &= ~(1<<CLOCKOUT_BIT);
    }
}
            
/******************************************************************************
* set_enable_output
*       0V = 0, 5V = 1 
*******************************************************************************/
static inline void set_enable_output(u8 level)
{
    if(level)
    {
        ENAOUT_PORT |= (1<<ENAOUT_BIT);
    }
    else
    {
        ENAOUT_PORT &= ~(1<<ENAOUT_BIT);
    }
}

/******************************************************************************
* radio_change_state
*        Change the state with interrupts disabled.
*        It's not clear if this is necessary.
*******************************************************************************/
static inline void radio_change_state(radio_state_t state)
{
    u8 flags = disable_interrupts();
    radio_context.state = state;
    restore_flags(flags);
}

/******************************************************************************
* radio_disable
*        
*******************************************************************************/
void radio_disable()
{
    DATAOUT_DIR &= (1<<DATAOUT_BIT);  
    CLOCKOUT_DIR &= (1<<CLOCKOUT_BIT);
    ENAOUT_DIR &= (1<<ENAOUT_BIT);
}    

/******************************************************************************
* radio_output_init
*        Initialize output pins and the output timer.
*******************************************************************************/
void radio_output_init()
{
    /* Config output pins */
    DATAOUT_DIR |= (1<<DATAOUT_BIT);  
    CLOCKOUT_DIR |= (1<<CLOCKOUT_BIT);
    ENAOUT_DIR |= (1<<ENAOUT_BIT);
    
    /**************************************************************************
    *      Configure T2 to clock the output, but don't begin counting.  
    *
    *      The two primary durations required are 40us and 65us.
    *      The clock period should be an even divisor of both.
    *      14745600/1024 = 69.44 us (bad)
    *              /128  = 8.68 us 
    *              /8    = 0.54 us (138 us max) *** /8 it is...
    *              /1    = 0.068 (too short - 17.3 us max)
    *      
    ***************************************************************************/

    /* TCCR2 B
     *  
     * /8 divisor
     */
    TCCR2B = ((1<<CS21));   

    radio_change_state(IDLE);
    radio_state_machine();    
}

/******************************************************************************
* radio_output_send_msg
*        If not currently transmitting a message,
*        copy the supplied bytes into the output buffer and
*        start sending.
*******************************************************************************/
u8 radio_output_send_msg(u8 a, u8 b, u8 c, u8 d, u8 e, u8 f, u8 g)
{
    if (radio_context.state != IDLE) // implement OFF here ?
    {
        /* try again; currently sending message */
        return 1;
    }
    
    radio_context.msg.octets[0] = a;
    radio_context.msg.octets[1] = b;
    radio_context.msg.octets[2] = c;
    radio_context.msg.octets[3] = d;
    radio_context.msg.octets[4] = e;
    radio_context.msg.octets[5] = f;
    radio_context.msg.octets[6] = g;    
    
    radio_change_state(START);
    radio_state_machine();
  
// avrtalk display mirror
//    send_msg(BROADCAST_NODE_ID, 0xDD, 7, radio_context.msg.octets, 0);
    
    return 0;
}

#if 0
/******************************************************************************
* radio_output_wait
*        
*******************************************************************************/
void radio_output_wait()
{
    while (radio_context.state != IDLE)
        ;
}
#endif

/******************************************************************************
* SIG_OUTPUT_COMPARE2
*       Timer 2 
*******************************************************************************/
SIGNAL(SIG_OUTPUT_COMPARE2A)
{
    radio_state_machine();
}
        

/******************************************************************************
* radio_state_machine
*        Assert outputs and set the timer based on what the current state is.
*        This should be called immediately after any change of state,
*        including when the timer expires. 
*******************************************************************************/
void radio_state_machine()
{
    u8              t = 0;  /* time to next state.  0 means no timer. */
    radio_state_t   n;      /* next state */    
    u8              flags;

    flags = disable_interrupts();
    
    TCNT2 = 0;
    
    // SHRINKME: test whether cascading if/else is smaller than switch
    // See e.g. __tablejump__ code in disassembly...
    
    switch(radio_context.state)
    {
        case OFF:
            // set outputs as inputs, pullups off
            n = OFF;
            break;
        case IDLE:
        case START:
            set_enable_output(0);
            set_clock_output(1);
            set_data_output(0); // 5v
            
            radio_context.bit = 0;
            radio_context.repeat = 0;

            if (radio_context.state == IDLE)
            {
                n = IDLE;
            }
            else
            {
                n = ENA_R1;
                /* waiting here enforces inter-message delay */
                t = T2_TICKS_100US;
            }
            break;
            
        case ENA_R1:
            set_enable_output(1);
            t = T2_TICKS_100US;
            n = ENA_F1;
            break;
        case ENA_F1:
            set_enable_output(0);
            t = T2_TICKS_100US;
            n = ENA_R2;
            break;
        case ENA_R2:
            set_enable_output(1);
            t = T2_TICKS_60US;
            n = CLK_F;
            break;
        case CLK_F:
        {
            u8 cb = radio_context.bit;
            u8 bv = radio_context.msg.octets[cb/8];
            bv &= (1<<(7-(cb%8))); // MSB first
            set_data_output(bv);
            set_clock_output(0);
            t = T2_TICKS_40US;
            n = CLK_R;
            break;
        }
        case CLK_R:
            set_clock_output(1);
            if (++radio_context.bit >= RADIO_MSG_BITS)
            {
                if (radio_context.repeat == 0)
                {
                    /* Repeat message again. */
                    t = T2_TICKS_40US;
                    n = ENA_F2;
                    radio_context.bit = 0;
                    radio_context.repeat = 1;
                }
                else
                {
                    /* End of data */
                    t = T2_TICKS_60US;
                    n = ENA_F3;
                }
            }
            else
            {
                t = T2_TICKS_65US;
                n = CLK_F;
            }
            break;
        case ENA_F2:
            set_enable_output(0);
            t = T2_TICKS_100US;
            n = ENA_R3;
            break;
        case ENA_R3:
            set_enable_output(1);
            t = T2_TICKS_60US;
            n = CLK_F;
            break;
        case ENA_F3:
            set_enable_output(0);
            set_data_output(0); // 5v
            n = POST;
            break;
	default:
	    n = IDLE;
    }

    //tmp
    TCNT2 = 0;

    
    if(t)
    {
        OCR2A = t;
        /* enable compare match interrupt */
        TIMSK2 |= (1<<OCIE2A);
    }
    else
    {
        /* disable compare match interrupt */
        TIMSK2 &= ~(1<<OCIE2A);
    }

    radio_context.state = n;

    restore_flags(flags);
}    

        
static task_t radio_output_taskinfo;
//static u8     radio_output_mailbox_buf[20]; /// <<<< this is too small!
static u8     radio_output_mailbox_buf[30];  /// <<<< not sure about this one 

u8 radio_task();

task_t *radio_output_task_create()
{
    radio_output_init();
    
    return setup_task(&radio_output_taskinfo, TASK_ID_RADIO_OUTPUT, radio_task,
            radio_output_mailbox_buf, sizeof(radio_output_mailbox_buf));
}

u8 radio_task()
{

    /* This runs immediately following the completion of a radio
     * message transmit.  This code disables interrupts for relatively
     * long periods of time and can cause issues with the radio
     * timing, leading to intermittent blanking of the screen. */
    if (radio_context.state == POST)
    {
        egt_read_thermocouple();
        radio_context.state = IDLE;
    }

    if (radio_context.state != IDLE)
    {
        /* Do not pull message from mailbox if idle.
         * This avoids dropping messages, but may
         * result in a backlog if messages are sent
         * too quickly. */
        return 0;
    }

    u8 payload_len, code;
    if (mailbox_head(&radio_output_taskinfo.mailbox, &code, &payload_len))
    {
        if (code == RADIO_MSG_SEND && payload_len == 7)
        {
            u8 msg[7];
            mailbox_copy_payload(&radio_output_taskinfo.mailbox,
                    msg, 7, 0);
            radio_output_send_msg(
                    msg[0], msg[1], msg[2], msg[3], msg[4], 
                    msg[5], msg[6]);
        }
        mailbox_advance(&radio_output_taskinfo.mailbox);
    }
    return 0;
}
        
            
