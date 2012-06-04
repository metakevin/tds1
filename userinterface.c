/******************************************************************************
* File:              userinterface.c
* Author:            Kevin Day
* Date:              February, 2005
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

#include <avr/io.h>
#include <avr/wdt.h>
#include "userinterface.h"
#include "types.h"
#include "timers.h"
#include "tasks.h"
#include "button.h"
#include "comms_generic.h"
#include "persist.h"
#include "audiradio.h"
#include "output.h"
#include "platform.h"
#include "radioin.h"
#include "egt.h"

#define MODE_BUTTON_DIR DDRC
#define MODE_BUTTON_PIN PINC
#define MODE_BUTTON_PORT PORTC
#define MODE_BUTTON_BIT 4

task_t           ui_taskinfo;
static u8        ui_mailbox_buf[40];
display_mode_t   display_modes[MAX_DISPLAY_MODES];
timerentry_t     display_update_timer;

u8               ui_in_config_flag;

struct {
    u8              mnum;                
    u8              stop_updates;
    u8              press_feedback;       /* show feedback pattern */
    u8              mode_just_changed;
} current;

void ui_display_callback(timerentry_t *ctx);

u8 ui_task();

ui_mode_t validate_mode(ui_mode_t mode)
{
    while (!(display_modes[mode%MAX_DISPLAY_MODES].flags & MODE_ENABLED))
    {
        ++mode;
        if (mode >= MAX_DISPLAY_MODES*2)
        {
            /* No modes active.  Use 1 as default. */
            mode = 1; 
            break;
        }
    } 
    return mode%MAX_DISPLAY_MODES;
}

typedef void (*bit_set_callback_t)(u16 param);

typedef struct {
    u8                  word : 4;
    u8                  bit  : 4;
    bit_set_callback_t  callback;
    u16                 param;
} config_bit_entry_t;

void set_mode_disable_bit_cb(u16 param)
{
    /* If bit set, disable mode */
    if (param < MAX_DISPLAY_MODES)
    {
        display_modes[param].flags &= ~MODE_ENABLED;
    }
}
#if 0
void unset_mode_disable_bit_cb(u16 param)
{
    /* If bit set, disable mode */
    if (param < MAX_DISPLAY_MODES)
    {
        display_modes[param].flags |= MODE_ENABLED;
    }
}
#endif

void set_parameter_cb(u16 param)
{
    *((u8 *)param) = 1;
}

u8 cluster_is_1995;
u8 peak_hold_time_10s;
#ifdef RADIO_IN_SUPPORT
u8 radio_change_override;
#endif

/* Words are numbered 0, 1, ... in order they are displayed to the user.
 * Bits are numbered LSB=0 to MSB=12 (values 0-8191) 
 * The value stored in flash is the complement of the value used
 * below and shown to the user -- so the default value is all
 * bits clear (flash 0xFFFF) */
config_bit_entry_t config_bits[] = {
    {0, 12, set_mode_disable_bit_cb, MODE_BOOST_INSTANT},                   // 4096
    {0, 11, set_mode_disable_bit_cb, MODE_BOOST_PEAK},                      // 2048
    {0, 10, set_mode_disable_bit_cb, MODE_ATMOSPHERIC},                     // 1024
    {0, 9 , set_mode_disable_bit_cb, MODE_IAT},                             // 512
    {0, 8 , set_mode_disable_bit_cb, MODE_IAT_PEAK},                        // 256
    {0, 7 , set_mode_disable_bit_cb, MODE_VOLTMETER},                       // 128
    {0, 5 , set_mode_disable_bit_cb, MODE_OILPRES},                         // 32
    {0, 1 , set_parameter_cb,        (u16)&peak_hold_time_10s},             // 2
    {0, 0 , set_parameter_cb,        (u16)&cluster_is_1995},                // 1
#ifdef RADIO_IN_SUPPORT
    {0, 6 , set_mode_disable_bit_cb, MODE_RADIO},                           // 64
    {0, 2 , set_parameter_cb,        (u16)&radio_change_override},          // 4
#endif
    {1, 0, set_mode_disable_bit_cb, MODE_FP_ABSOLUTE},                     // 1
    {1, 1, set_mode_disable_bit_cb, MODE_FP_RELATIVE},                     // 2
    {1, 2, set_mode_disable_bit_cb, MODE_FP_RELATIVE_TROUGH},              // 4
    {1, 3, set_mode_disable_bit_cb,  MODE_WIDEBAND},                         // 8
    {1, 4 , set_mode_disable_bit_cb, MODE_EGT_INSTANT},                     // 16
    {1, 5 , set_mode_disable_bit_cb, MODE_EGT_PEAK},                        // 32
    {1, 12 , set_mode_disable_bit_cb, MODE_DATALOGGER},                      // 4096
};

#define NUM_CONFIG_WORDS 2
void load_configuration_words()
{
    u16 config_words[NUM_CONFIG_WORDS];
    u8 i;
    for(i=0; i<NUM_CONFIG_WORDS; i++)
    {
        config_words[i] = ~load_persist_data_16(PDATA_CONFIG_WORD_1L + i*2);
    }

    for(i=0; i<sizeof(config_bits)/sizeof(config_bits[0]); i++)
    {
        if (config_words[config_bits[i].word] & (1<<config_bits[i].bit))
        {
            config_bits[i].callback(config_bits[i].param);
        }
    }
}

/* Additional configuration options:
 *  Longer peak-hold period - 5s or 10s
 *  Wider atmospheric voltage range (?)
 */


u8  mode_select_word;
u8  mode_select_digit; 
u16 mode_select_current;

void do_config_word(u8 w)
{
    mode_select_word = w;
    u16 f = ~load_persist_data_16(PDATA_CONFIG_WORD_1L + w*2);
    f &= 0x1FFF; /* 13 bits */
    mode_select_current = f;
    mode_select_digit = 0;  /* leftmost */
}

u8 mode_select_display_func(ui_mode_t mode, ui_display_event_t event)
{
    if (event == CONFIG_SHORT_PRESS)
    {
        u16 m = 1;
        u8 i;
        for(i=0; i<3-mode_select_digit; i++)
        {
            m *= 10;
        }
        u8 p = (mode_select_current/m)%10;
        mode_select_current -= p*m;
        p = (p+1)%10;
        mode_select_current += p*m;
    }
    else if (event == CONFIG_LONG_PRESS)
    {
        ++mode_select_digit;
        if (mode_select_digit >= 4)
        {
            /* Save the new config word */
            save_persist_data_16(PDATA_CONFIG_WORD_1L + mode_select_word*2, ~mode_select_current);
            
            ++mode_select_word;
            do_config_word(mode_select_word);
        }        
        if (mode_select_word >= NUM_CONFIG_WORDS)
        {
            /* wait for button release */
            while (!(LED_PIN & (1<<LED_BIT)));
            
            radio_disable();
            
            /* reset */
            void (*rstvec)() = (void (*)())0;
            (*rstvec)();
        }
    }
    output_number(MODE_MODE_SELECT + mode_select_word + 1, 
            mode_select_current, CENTER_NONE, DFLAGS_FLASH_PLACE_0<<mode_select_digit);
    return 1;
}

/* Note: this is not the first ui function to be called.
 *       Display modes are registered prior to this 
 *       function being called.
 *       In fact, all display modes must be registered
 *       before this function is called. */
task_t *ui_task_create(u8 start_mode)
{
#if 0
    /* These modes are not enabled by default */
    set_mode_disable_bit_cb(MODE_OILPRES);
    set_mode_disable_bit_cb(MODE_EGT_INSTANT);
    set_mode_disable_bit_cb(MODE_EGT_PEAK);
#endif
    
    if (start_mode == MODE_MODE_SELECT)
    {
        current.mnum = MODE_MODE_SELECT;
        display_modes[MODE_MODE_SELECT].displayfunc = mode_select_display_func;
        ui_in_config_flag = 1;
        do_config_word(0);
    }
    else
    {
        load_configuration_words();
        if (start_mode == MODE_ATMOSPHERIC)
        {
            /* Do not validate if we are starting up in atmospheric mode.
             * Even if it is disabled it should be displayed after measurement. */
            current.mnum = start_mode;
        }
        else
        {
            current.mnum = validate_mode(start_mode);
        }
    }

    ui_display_callback(NULL);

    return setup_task(&ui_taskinfo, TASK_ID_UI, ui_task, ui_mailbox_buf, sizeof(ui_mailbox_buf));
}

u8 ui_task()
{
    u8 payload_len, code;

    if (mailbox_head(&ui_taskinfo.mailbox, &code, &payload_len))
    {
        if (code == UI_MSG_STOP_UPDATES)
        {
            current.stop_updates = 1;

            send_msg(BROADCAST_NODE_ID,
                    TASK_ID_UI<<4|UI_MSG_STOP_UPDATES, 0, 0);
			
        }
        else if (code == UI_MSG_RESTART_UPDATES)
        {
            if (current.stop_updates == 1)
            {
                current.stop_updates = 0;
                ui_display_callback(NULL);
            }
        }
#if 0
        else if (code == UI_MSG_READ_ADC)
        {
            u16 adc = boost_read_raw_adc();
            send_msg(BROADCAST_NODE_ID, TASK_ID_UI<<4|UI_MSG_READ_ADC, 2, 
                    (u8*)&adc);
        }
#endif
        mailbox_advance(&ui_taskinfo.mailbox);
    }
    return 0;
}

void register_display_mode(ui_mode_t mode, display_func_t dfunc)
{
    u8 flags;

    if (mode >= MAX_DISPLAY_MODES)
    {
        /* error */
        return;
    }
    
    flags = disable_interrupts();    
    display_modes[(u8)mode].displayfunc = dfunc;
    display_modes[(u8)mode].flags       = MODE_ENABLED;
    restore_flags(flags);
}    

void send_feedback_msg()
{
    /* B / U */
    u8 msg[7] = {0x1C, 0x00, 0xff, 0xff, 0xff, 0xff, 0x02};
    send_to_task(TASK_ID_RADIO_OUTPUT, RADIO_MSG_SEND, 7, msg); 
}

u8 revert_counter;
#ifdef RADIO_IN_SUPPORT
u8 radio_change_counter;
#endif

void switch_to_mode(ui_mode_t mode)
{
    ui_mode_t last_mode = load_persist_data(PDATA_LAST_MODE);
    if (last_mode != current.mnum)
    {
        /* we "jumped" to this mode from e.g. the atmospheric
         * measurement.  Return to the previous mode. */
        mode = last_mode;
    }
    
    current.mnum = validate_mode(mode);

    current.mode_just_changed = 1;
}    

void ui_display_callback(timerentry_t *ctx)
{
#if 0
    if (display_update_timer.key)
    {
        egt_read_thermocouple();
        goto timer_again;
    }
#endif

    ui_mode_t mode = current.mnum;
	
	if (current.stop_updates)
    {
        /* Stop timer */
		return;
    }

#ifdef RADIO_IN_SUPPORT
    if (current.mode_just_changed)
    {
        radio_change_counter = 0;
    }
#endif
    
    if (current.press_feedback)
    {
        send_feedback_msg();
    }    
    else if (ui_in_config_flag)
    {
		(display_modes[mode].displayfunc)(mode, CONFIG_REFRESH);
    }
#ifdef RADIO_IN_SUPPORT
    else if (radio_change_counter && radio_change_override)
    {
        --radio_change_counter;
        radioin_display_func(mode, DISPLAY_NORMAL_UPDATE);
    }
#endif
    else    /* normal display */
    {
        (display_modes[mode].displayfunc)(mode, 
            current.mode_just_changed?DISPLAY_MODE_JUST_CHANGED:
                                      DISPLAY_NORMAL_UPDATE);
    }

    current.mode_just_changed = 0;
    
    if (revert_counter < 80 && !ui_in_config_flag)   /* 10 sec */
    {
        ++revert_counter;
        if (revert_counter == 80)
        {
            /* This will switch to the last mode iff
             * we jumped to the current mode automatically
             * (i.e. due to atmospheric measurement) */
            switch_to_mode(current.mnum);
        }
    }    

#if 0
timer_again:
    register_timer_callback(&display_update_timer, MS_TO_TICK(125),
            ui_display_callback, !display_update_timer.key);    
#else
    register_timer_callback(&display_update_timer, MS_TO_TICK(60), // was 125
            ui_display_callback, 0);    
#endif

}

void short_button_press()
{
    ui_mode_t mode = current.mnum;
    
    if (ui_in_config_flag)
    {
        /* Should never be in config mode unless mode is valid.
         * Assume it is. Also assume that there is a configfunc,
         * since if there wasn't longpress should have not entered
         * config mode.  */
        (display_modes[mode].displayfunc)(mode, CONFIG_SHORT_PRESS);
        /* Note: zero return code from short press will not quit
         * config mode.  Only zero return from enter or long press
         * will quit.  This could be changed. */
        return;
    }

    switch_to_mode(mode+1);
    
    /* Save last selected mode in EEPROM.
     * Note: this is the mode number, not the
     * index of that mode in display_modes. */
    save_persist_data(PDATA_LAST_MODE, current.mnum);
}

void long_button_press_feedback(u8 v)
{
    current.press_feedback = v;
}

void long_button_press()
{
    /* If config handler registered for this mode
     *  Call handler with state = ENTER
     *  Change short button press handler to 
     *  call handler with state = SHORTPRESS
     *  On long press call handler with state = EXIT
     *
     *  Must factor out menu scrolling, etc. to
     *  generic functions.
     */

    u8 ret = 0;
    if (!ui_in_config_flag)
    {
        ui_in_config_flag = 1;
        ret = (display_modes[current.mnum].displayfunc)(current.mnum,
                                             CONFIG_SCREEN_ENTER);
    }
    else
    {
        ret = (display_modes[current.mnum].displayfunc)(current.mnum,
                                             CONFIG_LONG_PRESS);
    }                        
    if (ret == 0)
    {
        /* Exit config mode */
        ui_in_config_flag = 0;
    }
}

#if 0
void favorite_button_press()
{
    if (current.infavorite)
    {
        current.mnum = current.favsave;
    }
    else
    {
        current.favsave = current.mnum;
        current.mnum = 

#endif
