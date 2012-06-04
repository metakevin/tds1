/******************************************************************************
* File:              main.c
* Author:            Kevin Day
* Date:              February, 2005
* Description:       
*                    Main program for audi radio interface
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


#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/io.h>
#include <avr/wdt.h>

#include "types.h"
#include "tasks.h"
#include "comms_generic.h"
#include "timers.h"
#include "button.h"
#include "adc.h"
#include "audiradio.h"
#include "boost.h"
#include "voltmeter.h"
#include "userinterface.h"
#include "persist.h"
#include "radioin.h"
#include "output.h"
#include "onewire.h"
#include "egt.h"
#include "datalogger.h"
#include "fuelpressure5v.h"

#define ANT_PORT PORTC
#define ANT_DIR  DDRC
#define ANT_PIN  PINC
#define ANT_BIT  4

void debug_led(u8 val);
void antenna_relay(u8 active);
void button_callback(button_context_t *b);
static inline u8 check_for_atmospheric();
u8 voltmeter_out_of_range_11_5_to_12_5();
void bufferpool_init();
task_t* comms_task_create();
task_t *radio_input_task_create();
void iat_init(adc_context_t *adc_context);
void radio_output_init();
void boost_load_atmospheric();
void boost_store_atmospheric();
void oilpres_init(adc_context_t *adc_context);
u8 check_startup_button();
#ifdef MULTIBUTTON
uint16_t multibutton_last_adc;
static inline void multibutton_init(adc_context_t *adc_context);
#else
static inline void start_blink_timer();
static inline void init_button();
#endif

#ifdef MULTIBUTTON
#define ADC_CONTEXTS 6
#else
#define ADC_CONTEXTS 5
#endif
adc_context_t adc_context[ADC_CONTEXTS];
volatile u8 startup_button_counter;
timerentry_t startup_timer;

/* Meh.  This consumes 8 bytes of code
 * to save 2 bytes of data.  */
volatile struct {
    u8 have_seen_button_off : 1;
    u8 force_atmospheric    : 1;
    u8 splash_flag          : 1;
} mainflags;


int main()
{    
    wdt_disable();

    /* Make all unused IOs outputs */
    /* Unused: C2, C3, [D0, D1 if no serial], D2, D4, B0, B1, B3, B4, B5 */

    DDRB  = _BV(0) | _BV(1) | _BV(3) | _BV(4) | _BV(5);
    PORTB = 0x00;
    DDRC  = 0x00;
    PORTC = 0x00;
    DDRD  = _BV(0) | _BV(1) | _BV(2) | _BV(4);
    PORTD = 0x00;
    
    
    antenna_relay(0);
    
    bufferpool_init();
    
    init_persist_data();
    
    systimer_init();

    /* Enable interrupts */
    sei();

    /* Populate the tasklist in priority order */    
    tasklist[num_tasks++] = comms_task_create();

    tasklist[num_tasks++] = radio_output_task_create();

#ifdef RADIO_IN_SUPPORT
    tasklist[num_tasks++] = radio_input_task_create();
#endif

#ifdef ONEWIRE_TASK
    tasklist[num_tasks++] = onewire_task_create();
#endif

#ifdef LOGGING_SUPPORT
    tasklist[num_tasks++] = data_logger_task_create();
#endif

    egt_init();
    
    /* Set up the ADC clients */
    boost_gauge_init (&adc_context[0]);
    voltmeter_init   (&adc_context[1]);
    iat_init         (&adc_context[2]);
    oilpres_init     (&adc_context[3]);
    fp_init          (&adc_context[4]);
#ifdef MULTIBUTTON
    multibutton_init (&adc_context[5]);
#endif
    
    /* Start the ADC */
    adc_init_adc(ADC_DIV128, ADC_CONTEXTS, adc_context);

    {
        u8 mode;
        
        if (check_startup_button())
        {
            mode = MODE_MODE_SELECT;
        }
        else if (check_for_atmospheric())
        {
            mode = MODE_ATMOSPHERIC;
        }
        else
        {
            mode = load_persist_data(PDATA_LAST_MODE);
        }
        
        /* Create the user interface task.
         * This must be done after all UI functions are registered */
        tasklist[num_tasks++] = ui_task_create(mode);
    }    
    
#ifndef MULTIBUTTON
    start_blink_timer();
    init_button(); 
#endif
    
//    send_msg(BROADCAST_NODE_ID, 0x11, 0, 0);
    
    /* non-preemptive static priority scheduler */    
    while(1)
    {
        u8 taskidx;
        for(taskidx=0; taskidx<num_tasks; taskidx++)
        {
            tasklist[0]->taskfunc(); // comms 
            tasklist[taskidx]->taskfunc();
        }
    }
}

#ifndef MULTIBUTTON
timerentry_t blink_timer;
timerentry_t button_timer;

//u8 dsflag;
void blink_callback(timerentry_t *t)
{
    u16 ms;
    if (t->key == 0)
    {
        debug_led(0);
        ms = 995;

//        ow_2760_write_reg(8, dsflag?0xFF:0);
//        dsflag=!dsflag;
    }
    else
    {
        debug_led(1);
        ms = 5;
    }

    register_timer_callback(&blink_timer, MS_TO_TICK(ms), blink_callback,
            !t->key);
}

static inline void start_blink_timer()
{
    blink_callback(&blink_timer);
}

void short_button_press(); // in userinterface.c
void long_button_press();
void long_button_press_feedback(u8 v);

/* Every 5ms, switch the LED to an input and
 * check if it's low.  It should be high if no
 * button is pressed, because the LED should
 * pull it to 5-Vf (>Von) */
void button_timer_callback(timerentry_t *t)
{
    u8 pin;
    static u16 ctr;
    u16 ms = 2;

    /* Don't sample when actively driving the LED low (on)
     * there isn't enough time for the line to float up
     * without putting a delay between the dir switch
     * and the sample. */
    
    if (LED_DIR & (1<<LED_BIT))
    {
        goto again;
    }
    
    pin = (LED_PIN & (1<<LED_BIT));

    if (!pin)
    {
        /* low.  increment counter with saturate */
        if (ctr < 65535)
        {
            /* give feedback if ctr > 100 */
            ++ctr;
            if (ctr >= 250 && ctr < 2500)
            {
                long_button_press_feedback(1);
            }
            else
            {
                long_button_press_feedback(0);
            }
        }
    }
    else
    {
        /* high.  check how many counts (5s of ms)
         * the button was low.  */
        long_button_press_feedback(0);
        
        /* Do not treat a hold-from-poweron button release as an event */
        if (mainflags.have_seen_button_off)
        {
            if (ctr >= 250)
            {
                /* >2s -- long press */
                long_button_press();
            }
            else if (ctr >= 3)
            {
                /* >30ms -- short press */
                short_button_press();
            }
        }
        else if (ctr >= 2500)
        {
            radio_disable();
            /* button held down for a long time since startup (past point of
             * feedback) -- erase persistent data */
            erase_persist_data();            
            /* reset */
            void (*rstvec)() = (void (*)())0;
            (*rstvec)();            
        }
            
        mainflags.have_seen_button_off = 1;
        ctr = 0;
        ms = 100;
    }
    
again:
    register_timer_callback(&button_timer, MS_TO_TICK(ms), 
            button_timer_callback, ctr);
}
        
static inline void init_button()
{
    register_timer_callback(&button_timer, MS_TO_TICK(10), 
            button_timer_callback, 0);
}

/* On the TDS the button & LED share a pin.
 * The button will short the pin to ground when pressed.
 * To turn the LED off, change the pin to an input rather
 * than outputting 5v (which could be shorted to gnd) */
void debug_led(u8 val)
{
    if (!val)
    {
        LED_DIR &= ~(1<<LED_BIT);
    }
    else
    {
        LED_DIR |= (1<<LED_BIT);
        LED_PORT &= ~(1<<LED_BIT);
    }
}

#else
void button_adc_callback(adc_context_t *ctx);
static inline void multibutton_init(adc_context_t *adc_context)
{
    adc_context->pin = LED_PIN;
    adc_context->ref = ADC_AVCC;
    adc_context->enabled = 1;
    adc_context->callback = button_adc_callback;
}    

void short_button_press(); // in userinterface.c
void long_button_press();
void long_button_press_feedback(u8 v);

/******************************************************************************
* button_adc_callback
*        An ADC callback is made every ADCPRESCALER*13 + n CPU cycles.
*        {n: cycles used in ADC ISR}
*        This is roughly once every 105 microseconds (90us + 280 cycles).
*        There are 6 enabled ADC channels.
*        So this callback is called every 630 us, or at ~1.5kHz.
*        The original binary button timer ran at 500Hz.
*******************************************************************************/
void button_adc_callback(adc_context_t *ctx)
{
    u16 adc = ctx->sample;


    { /* TEMP */ 
        static u16 msgctr;
        if (++msgctr >= 750)
        {
            send_msg(BROADCAST_NODE_ID, 0xDE 2, &adc, 0);
            msgctr = 0;
        }
    }


    if (adc < 100)
    {
        /* low.  increment counter with saturate */
        if (ctr < 65535)
        {
            ++ctr;
            if (ctr >= 750 && ctr < 7500)
            {
                long_button_press_feedback(1);
            }
            else
            {
                long_button_press_feedback(0);
            }
        }
    }
    else if (adc < 400)
    {
    }
    else
    {
        /* high.  check how many counts (5s of ms)
         * the button was low.  */
        long_button_press_feedback(0);
        
        /* Do not treat a hold-from-poweron button release as an event */
        if (mainflags.have_seen_button_off)
        {
            if (ctr >= 750)
            {
                /* >2s -- long press */
                long_button_press();
            }
            else if (ctr >= 9)
            {
                short_button_press();
            }
        }
        else if (ctr >= 7500) /* 5 sec */
        {
            radio_disable();
            /* button held down for a long time since startup (past point of
             * feedback) -- erase persistent data */
            erase_persist_data();            
            /* reset */
            void (*rstvec)() = (void (*)())0;
            (*rstvec)();            
        }
            
        mainflags.have_seen_button_off = 1;
        ctr = 0;
    }
}
#endif

void antenna_relay(u8 active)
{
    ANT_DIR |= (1<<ANT_BIT);
    if (active)
    {
        ANT_PORT |= (1<<ANT_BIT);
    }
    else
    {
        ANT_PORT &= ~(1<<ANT_BIT);
    }
}

#if 0
u8 test_seqs[][7] = {
    {0x1e, 0x00, 0x30, 0x39, 0x32, 0x33, 0x00}, /*  92.3 FM */
    {0x1a, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM     Stereo */
    {0x8A, 0x00, 0x31, 0x32, 0x36, 0x30, 0x00}, /* 1260  AM */
    {0x8E, 0x00, 0x31, 0x32, 0x36, 0x30, 0x00}, /* 1260  AM */
    {0x8A, 0x40, 0x30, 0x38, 0x36, 0x00, 0x00}, /* TAPE */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x10}, /* 100.1 FM 1-1 Stereo */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x20}, /* 100.1 FM 1-2 Stereo */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x30}, /* 100.1 FM 1-3 Stereo */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x40}, /* 100.1 FM 1-4 Stereo */
    {0x1e, 0x02, 'T' , 'E' , 'S' , 'T' , 0x00}, /* 453.4 FM     Stereo */
//               0x54, 0x45, 0x53, 0x54
    {0x1a, 0x00, 'k' , 'd' , 'a' , 'y' , 0x00}, /* R41.9 FM */
//               0x6b, 0x64, 0x61, 0x79
    {0x2e, 0x00, 0x30, 0x39, 0x32, 0x33, 0x00}, /*  923  AM */
    {0x4e, 0x00, 0x30, 0x39, 0x32, 0x33, 0x00}, /*  RDS  AM */
    {0x01, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001   M */
    {0x02, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  AM */
    {0x04, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001   M */
    {0x08, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001   M */
    {0x10, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1  U */
    {0x20, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001   M */
    {0x40, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* RDS    M */
    {0x80, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001   M */
    {0x1e, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM */
    {0x2e, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  AM */
    {0x4e, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* RDS   AM */
    {0x8e, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  AM */
    {0x1E, 0x01, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM */
    {0x1E, 0x02, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM Stereo */
    {0x1E, 0x04, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM */
    {0x1E, 0x08, 0x31, 0x30, 0x30, 0x31, 0x00}, /* TR 10 CD 01 */    
    {0x1E, 0x10, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM */    
    {0x1E, 0x20, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM */    
    {0x1E, 0x40, 0x31, 0x30, 0x30, 0x31, 0x00}, /* TAPE */    
    {0x1E, 0x80, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x50}, /* 100.1 FM Stereo 1-5 */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x60}, /* 100.1 FM Stereo 1-6 */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x70}, /* 100.1 FM Stereo 1-7 */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x80}, /* 100.1 FM Stereo 1-8 */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0xA0}, /* 100.1 FM Stereo 1-M */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0xB0}, /* 100.1 FM Stereo 1-U */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0xD0}, /* 100.1 FM Stereo 1-F */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0xE0}, /* 100.1 FM Stereo 1-/^ (M) */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x01}, /* 100.1 FM Stereo */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x02}, /* 100.1 FM Stereo */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x04}, /* 100.1 FM Stereo */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x08}, /* 100.1 FM Stereo */
    {0x1a, 0x02, 0x3A, 0x3B, 0x3C, 0x3D, 0x00}, /* DRS.T FM Stereo */
    {0x1a, 0x02, 0x3E, 0x3F, 0x30, 0x30, 0x00}, /* ) 0.0 FM Stereo */
    {0x1A, 0x00, 0x3D, 0x31, 0x36, 0x34, 0x00}, /* T16.4 FM */
    {0x1A, 0x02, 0x3D, 0x31, 0x34, 0x35, 0x00}, /* T14.5 FM Stereo */
    {0x1A, 0x00, 0x3D, 0x32, 0x33, 0x31, 0xA0}, /* T23.1 FM 1-M */
    {0x1E, 0x00, 0x3D, 0x30, 0x30, 0x30, 0xB0}, /* T00.0 FM 2-U */
    {0x1A, 0x00, 0x30, 0x32, 0x33, 0x31, 0x00}, /* 23.1  FM */
    {0x1A, 0x00, 0x32, 0x34, 0x31, 0x34, 0xD0}, /* 241.4 FM 1-F */
    {0x8e, 0x00, 0x3A, 0x30, 0x31, 0x3D, 0x00}, /* D01T  AM */
    {0x8e, 0x00, 0x3D, 0x3F, 0x3F, 0x3F, 0x00}, /* T     AM */
    {0x8e, 0x00, 0x3F, 0x3D, 0x3F, 0x3F, 0x00}, /*  T    AM */
    {0x8e, 0x00, 0x3F, 0x3F, 0x3D, 0x3F, 0x00}, /*   T   AM */
    {0x8e, 0x00, 0x3F, 0x3F, 0x3F, 0x3D, 0x00}, /*    T  AM */
    {0x8e, 0x00, 0x31, 0x3F, 0x3F, 0x3F, 0x00}, /* 1     AM */
    {0x8e, 0x00, 0x3F, 0x31, 0x3F, 0x3F, 0x00}, /*  1    AM */
    {0x8e, 0x00, 0x3F, 0x3F, 0x31, 0x3F, 0x00}, /*   1   AM */
    {0x8e, 0x00, 0x3F, 0x3F, 0x3F, 0x31, 0x00}, /*    1  AM */
    {0x1E, 0x00, 0x3F, 0x31, 0x33, 0x37, 0xB0}, /*  13.7 FM 2-U */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0x90}, /* 100.1 FM Stereo 1-9 */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0xC0}, /* 100.1 FM Stereo 1-V */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0xF0}, /* 100.1 FM Stereo 1-  */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0xCF}, /* 100.1 FM Stereo 1-V */
    {0x1a, 0x02, 0x31, 0x30, 0x30, 0x31, 0xFF}, /* 100.1 FM Stereo 1-  */
    {0x11, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 U   */
    {0x13, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM  */
    {0x15, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 U   */
    {0x17, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM  */
    {0x1A, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM  */
    {0x1C, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 U   */
    {0x1F, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 FM  */
    {0x21, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  U   */
    {0x22, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  AM  */
    {0x23, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  AM  */
    {0x24, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  M   */
    {0x25, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  M   */
    {0x26, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  AM  */
    {0x2F, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 1001  AM  */
    {0x11, 0x00, 0x30, 0x31, 0x32, 0x32, 0xC0}, /* 12.2 U V */
    {0x00, 0x00, 0x30, 0x31, 0x32, 0x32, 0xC0}, /* 122 M */
    {0x00, 0x00, 0x30, 0x39, 0x39, 0x39, 0x00}, /* 999 M   */
    {0x11, 0x00, 0x31, 0x30, 0x30, 0x31, 0x00}, /* 100.1 U   */
    {0x04, 0x00, 0x31, 0x30, 0x31, 0x33, 0x00}, /* 1013   M */
    {0x04, 0x00, 0x32, 0x36, 0x30, 0x36, 0x00}, /* 2606   M */
    {0x14, 0x00, 0x30, 0x32, 0x33, 0x31, 0x00}, /* 23.1 U   */
    {0x14, 0x00, 0x30, 0x32, 0x33, 0x31, 0xD0}, /* 23.1 UF  */
    {0x1A, 0x00, 0x32, 0x34, 0x31, 0x3D, 0xD0}, /* 241.T FM 1-F */
    {0x11, 0x00, 0x32, 0x34, 0x31, 0x3D, 0xD0}, /* 241.T UF */
    {0x11, 0x00, 0x32, 0x34, 0x31, 0x30, 0xD0}, /* 241.0 UF */
    {0x14, 0x00, 0x30, 0x32, 0x33, 0x31, 0xA0}, /* 23.1 M  ? */
    {0x14, 0x00, 0x30, 0x32, 0x33, 0x31, 0xE0}, /* 23.1   ? */

    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x00}, /* 1 M */
    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x01}, /* 1 M1 */
    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x09}, /* 1 M9 */
    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x0A}, /* 1 MM */
    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x0B}, /* 1 MU */
    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x0C}, /* 1 MV */
    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x0D}, /* 1 MF */
    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x0E}, /* 1 M (half M) */
    {0x04, 0x00, 0x30, 0x30, 0x30, 0x31, 0x0F}, /* 1 M_ (space) */
 
/*
 *      
 *  r 4 0 30 30 30 31 f         001 M (m centered)
 *  r 4 0 30 30 30 31 ff        001 M (m to left)
 *  r 4 0 30 30 30 31 1f         001 M1 
 *  r 4 0 30 30 30 31 2f         001 M2 
 *  r 4 0 30 30 30 31 af         001 MM 
 *
 *  r 14 0 30 30 30 31 af       00.1  UM
 *  r 14 0 30 30 30 31 a0       00.1  UM
 *  r 14 0 30 30 30 31 00       00.1  U
 *  r 1c 2 30 31 35 31 dc       15.1  UF
 *
 *  r 3c 0 30 30 30 31 a0       00.1  VM
 *  r 3c 0 30 30 30 31 10       00.1  V1
 *  r 3c 2 30 30 30 31 10       00.1 (stereo) V1
 *  r 1c 2 30 31 35 31 11       15.1 A  U1 
 *  ...
 *  r 1c 2 30 31 35 31 16       15.1 F  U1
 *  r 1c 2 30 31 35 31 60       15.1 (stereo) U6
 *  r 1c 2 30 31 35 31 10       15.1 (stereo) U1
 *  r 1c 2 30 37 30 30 63       70.0 C U
 *  r 1c 2 30 37 30 30 96       70.0 F U9
 *
 *  r fc 2 30 37 30 30 96       RDS   F  V9
 *
 *  r b8 11 40 45 50 55 00      50.5  V      (voltmetera
 *  r b0 00 1 2 3 4 00          123.4 V
 *
 *  r b0 00 1 2 3 4 81          123.4 / A / V8
 *
 *  r a0 00 1 2 3 4 11          1234 / A / M1
 *  r a0 00 1 2 3 4 1          1234 / A / M
 *
 *  r a3 3 1 2 3 4 0            1234 / (stereo) / AM
 *  r a5 3 1 2 3 4 0            1234 / (stereo) / M
 *
 *r 10 0 ff ff ff ff 06     F / U
 *r 80 0 00 dd aa cc 00   TDS   /  /  M

 68 00 c4 c0 dc c4 00 RDS M  (came from head unit!)

r dd 3 1 2 3 f4 00      RDS (stereo) U
 
 *  
 *  r 3c 8f 30 30 30 31 10      TR00 CD 01
 *  r 0x00, 0x3f, 0x3D, 0x3d, 0x3d, 0x3d, 0x0  TR==CD== (96)
 *r 3c ff 30 30 30 31 10        TAPE
 
 vac:

r 14 00 3c 32 35 37 10      S25.77 U1
r 1c 02 00 32 35 37 00      25.7  (stereo)  U



};
 
 *
 */


    
#endif


void splash_timer_callback(timerentry_t *p)
{
    mainflags.splash_flag = 1;
}


/******************************************************************************
* check_for_atmospheric
*        This function runs before the user interface and task scheduler
*        are running.  
*******************************************************************************/
u8 check_for_atmospheric()
{
    /* Display splash screen for 2s, while monitoring the voltmeter.
     * If the voltage does not exceed 12.5v, do an atmospheric
     * reading and alert the user.  Store the atmospheric reading
     * to eeprom. */

    /* 5 seconds is a long time for the splash screen, but if 
     * engine cranking is detected it will be aborted. */

    timerentry_t splash_timer;
    
    register_timer_callback(&splash_timer, MS_TO_TICK(5000), splash_timer_callback, 0);
 
//    radio_output_send_msg(0x14, 0x00, 0x3D, 0x3A, 0x3C, 0x31, 0x00);

    while (!mainflags.splash_flag)
    {        
        if ((voltmeter_out_of_range_11_5_to_12_5()) && !mainflags.force_atmospheric)
        {
            /* Load saved value; exit atmospheric measurement mode */
            boost_load_atmospheric();
            return 0;
        }
    };
        
    boost_store_atmospheric();
    return 1;
}        
    
void startup_timer_callback(timerentry_t *p)
{
    ++startup_button_counter;
    register_timer_callback(&startup_timer, MS_TO_TICK(250), startup_timer_callback, 0);
}

u8 check_startup_button()
{
    u8 pin;
    u8 ret = 0;

    LED_DIR &= ~(1<<LED_BIT);

    startup_timer_callback(0);
    
    do {        
        pin = (LED_PIN & (1<<LED_BIT));
    } while (!pin && startup_button_counter < 8);

    if (startup_button_counter >= 8)
    {
        ret = 1;
    }
    else if (startup_button_counter >= 2)
    {
        mainflags.force_atmospheric = 1;
    }

    remove_timer_callback(&startup_timer);
    return ret;
}


