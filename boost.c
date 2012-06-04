/******************************************************************************
* File:              boost.c
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

#include "boost.h"
#include "adc.h"
#include "timers.h"
#include "comms_generic.h"
#include "tasks.h"
#include "audiradio.h"
#include "userinterface.h"
#include "persist.h"
#include "avrsys.h"
#include "output.h"
#include "radioin.h"
#include "datalogger.h"

#define BLINK_WRAP 8    // 1000ms -- see display_update_timer
#define BLINK_MID  4    // 500ms
#define BLINK_QUARTER_MASK  2   // changes every 250ms



/*
 *    2) If you go to the moreboost.com and look under VMAP4 (old product
   you will see some graphs that may help). For an average Bosch sensor
   (they can vary quite a bit +/- 0.1 V) 4.65 volts = 3.0 bar absolute
   (assuming a source voltage of 5 volts). They are relatively linear so
   that is not an issue and offset is slightly positive but for all
   practical purposes I would use 0.

   2) Calibrating at a few spots or even at standard atmosphere with a
   precision barometer should work well enough better than 0.5 PSI full
   range with the 3.0 bar Bosch Sensor ... or buy a VMAP set it to
   precision mode and run your circuit will be the best ... a little
   costly for a pressure gauge but should be very accurate.
   3) No need to do an aliasing filter - the response from the turbo is
   really the controlling factor - you may see some wastegate "flutter"
   caused by the WGFV and the wastegate spring "resonating" if you will
   ... spring should be tightened when this happens... I think it will be
   around 15 Hz ... I keep meaning to measure it ... bet never have ...
   it should be proportional to WGFV ... so you may want to take a few
   samples over a 0.1 second interval and average them.
   Best regards,
   Feico van der Laan

*/

#ifdef RADIO_IN_SUPPORT
extern u8 blank_screen_msg[7];
extern u8 last_valid_msg[7];
extern u8 is_valid;
#endif

void boost_gauge_adc_callback(adc_context_t *ctx);
void boost_update_callback(timerentry_t *ctx);
timerentry_t boost_timer;
u8 boost_display_func(ui_mode_t mode, ui_display_event_t event);
static inline u8 boost_config_func(ui_mode_t mode, ui_display_event_t event);
void output_boost(u16 avg_adc10q6, u8 mode, u8 is_peak, u8 ignore_radio);

#define BOOST_UPDATE_MS                 250
#define BOOST_UPDATE_PERIOD             MS_TO_TICK(BOOST_UPDATE_MS)
#define DEFAULT_PEAK_AGE                4*5 /* in units of UPDATE_MS */
#define LONG_PEAK_AGE                   4*10

extern u8 peak_hold_time_10s;
#define PEAK_AGE (peak_hold_time_10s?LONG_PEAK_AGE:DEFAULT_PEAK_AGE)

#define BOOST_PIN 6

#define PSIG 1

typedef enum {
    ATMOS_ERROR, ATMOS_DEFAULT, ATMOS_LOADED, ATMOS_MEASURED
} atm_status_t;

//     OUTPUT_MODE_PSIG_ONLY=2, OUTPUT_MODE_PSIG_ONLY_STAYON=3, OUTPUT_MODE_PSIG_ONLY_RADIO=4,

#ifdef RADIO_IN_SUPPORT
typedef enum {
    OUTPUT_MODE_INHG_PSIG=0,
    OUTPUT_MODE_ABSOLUTE=1,
#ifndef NINETYFIVE_IC
	OUTPUT_MODE_PSIG_ONLY_RADIO=2,
#endif
    OUTPUT_MODE_INVALID
} output_mode_t;
#else
typedef enum {
    OUTPUT_MODE_INHG_PSIG=0,
    OUTPUT_MODE_ABSOLUTE=1,
    OUTPUT_MODE_INVALID
} output_mode_t;
#endif

#define SAMPLES_PER_ACCUM_2N 6
typedef struct {
    u16          sample_accumulator;
    u16          current_boost_accum;
    u16          peak_boost_accum;
    u16          atmospheric10q6;    
    u8           atmos_counter;
    u8           peak_counter;
    u8           boost_accum_valid;
    output_mode_t output_mode;
  #define SAMPLES_PER_AVG 64
    u8           sample_counter;
    atm_status_t atmospheric_status;
    u8           blink_counter;    
    u8           map_sensor_idx;
    u8           mode_change_downcounter;
    enum {CONFIG_MAP, CONFIG_UNITS} configure_mode;
    
} boost_context_t;


boost_context_t boost_context;


/******************************************************************************
* current_boost_accum holds the average value times 2^SAMPLES_PER_ACCUM_2N.
*        Or, the average in Q10.6 fixed point format.
*        Given the accuracy of the transducer, there's no point in
*        that resolution.  For display purposes, we want resolution to
*        the millibar (1/3000 of FS) or 0.1 PSI (1/435 of FS).
*        Given that FS is (according to feico) 4.65V, or 951, 
*        a lot of the fractional bits won't be very significant.
*******************************************************************************/

/* one inHg = 33.86 mbar
 * one bar  = 29.53 inHg 
 * CODE_1BAR*64 / 29.53 = 687.03
 *
 *  (adc*64) / ((CODE_1BAR*64)/29.53) = inHg
 *  adc_accum / CODE_INHG_10Q6 = inHg
 */

/* 
 * 1 bar = 14.5037738 pounds per square inch
 *
 * CODE_1BAR*64  / 14.5037738 =   1398.808
 * CODE_1BAR*512 / 14.5037738 =  22380.93  (eh.)
 */
#define CODE_PSI_10Q6 map_constants[boost_context.map_sensor_idx].code_psi_10q6
#define DIV_1BAR_16Q0 map_constants[boost_context.map_sensor_idx].code_bar_16q0
#define CODE_INHG_10Q6 map_constants[boost_context.map_sensor_idx].code_inhg_10q6
#define CODE_FULLSCALE \
  ((map_constants[boost_context.map_sensor_idx].code_bar_16q0 * \
   map_constants[boost_context.map_sensor_idx].maxbars_x10)/10)

/* 1013.25 millibars : 1.01325*CODE_1BAR*64 = 20557 */
//#define STD_ATMOSPHERE_10Q6 20557

//#define STD_ATMOSPHERE_10Q6 (64.848*DIV_1BAR_16Q0)
#define STD_ATMOSPHERE_10Q6 map_constants[boost_context.map_sensor_idx].adc_std_atmospheric
  
/* 0.65 bar */
//#define ATMOSPHERIC_MIN_SANE_10Q6 13187

//#define ATMOSPHERIC_MIN_SANE_10Q6 (41.6*DIV_1BAR_16Q0)
#define ATMOSPHERIC_MIN_SANE_10Q6 map_constants[boost_context.map_sensor_idx].adc_atmos_min_sane
  
/* 1.1 bar */
//#define ATMOSPHERIC_MAX_SANE_10Q6 22317

//#define ATMOSPHERIC_MAX_SANE_10Q6 (70.4*DIV_1BAR_16Q0)
#define ATMOSPHERIC_MAX_SANE_10Q6 map_constants[boost_context.map_sensor_idx].adc_atmos_max_sane

  
typedef struct {
    u8      maxbars_x10;        /* e.g. 30 for 3.0 bar */
    u16     code_bar_16q0;
    u16     code_psi_10q6;
    u16     code_inhg_10q6;
    s16     adc_offset_10q6;
    u16     adc_std_atmospheric;
    u16     adc_atmos_min_sane;
    u16     adc_atmos_max_sane;
} map_sensor_constants_t;

/********************** Move this table to EEPROM to save code space *****************/


/* 4.65 volts = code 951     (951.39)
 * code_bar = 951 / (bars at 4.65 volts)
 * code_psi = code_bar * 64 / 14.5037738
 * code_inhg = code_bar * 64 / 29.53
 */

#define MAX_CONSTANT 75
#define MIN_CONSTANT 40

/* Last three fields should be
 * code_bar *  (64.848, 41.6, 70.4) */
const map_sensor_constants_t map_constants[] = {
    {   25, 409, 1807, 887,  2752, 409L*64.848, 409L*MIN_CONSTANT, 409L*MAX_CONSTANT}, /* 2.5 bar, calibrated */
    {   26, 382, 1614, 793,  1706, 382L*64.848, 382L*MIN_CONSTANT, 382L*MAX_CONSTANT}, /* 2.6 bar, interpolated from 2.5/3.0 cal. */
    {   27, 359, 1554, 763,  800 , 359L*64.848, 359L*MIN_CONSTANT, 359L*MAX_CONSTANT}, /* 2.7 bar, interpolated from 2.5/3.0 cal. */
    {   28, 338, 1499, 736, -1   , 338L*64.848, 338L*MIN_CONSTANT, 338L*MAX_CONSTANT}, /* 2.8 bar, interpolated from 2.5/3.0 cal. */
    {   29, 320, 1447, 711, -714 , 320L*64.848, 320L*MIN_CONSTANT, 320L*MAX_CONSTANT}, /* 2.9 bar, interpolated from 2.5/3.0 cal. */
    {   30, 303, 1338, 657, -1344, 303L*64.848, 303L*MIN_CONSTANT, 303L*MAX_CONSTANT}, /* 3.0 bar, calibrated */
    {   31, 288, 1354, 665, -1930, 288L*64.848, 288L*MIN_CONSTANT, 288L*MAX_CONSTANT}, /* 3.1 bar, interpolated from 2.5/3.0 cal. */
    {   32, 275, 1311, 644, -2453, 275L*64.848, 275L*MIN_CONSTANT, 275L*MAX_CONSTANT}, /* 3.2 bar, interpolated from 2.5/3.0 cal. */
    {   20, 476, 2098, 1031, 0   , 476L*64.848, 476L*MIN_CONSTANT, 476L*MAX_CONSTANT}, /* 2.0 bar */
//    {  130, 322, 1443, 704,  231, 
};

    

/******************************************************************************
* boost_gauge_init
*     I don't think this needs to be a task.   
*     The adc context must point to a slot in the structure which
*     will be given to adc_init_adc.  It will be filled in here.
*******************************************************************************/
void boost_gauge_init(adc_context_t *adc_context)
{
    adc_context->pin      = BOOST_PIN;
    adc_context->enabled  = 1;
    adc_context->ref      = ADC_AVCC;
    adc_context->callback = boost_gauge_adc_callback;

    boost_context.map_sensor_idx = load_persist_data(PDATA_MAP_SENSOR_INDEX);
    if (boost_context.map_sensor_idx >= sizeof(map_constants)/sizeof(map_sensor_constants_t))
    {
        boost_context.map_sensor_idx = 0;   /* 2.5 bar */
    }

    boost_context.output_mode = load_persist_data(PDATA_MAP_UNITS);
    if (boost_context.output_mode >= OUTPUT_MODE_INVALID)
    {
        boost_context.output_mode = OUTPUT_MODE_INHG_PSIG;
    }

    register_display_mode(MODE_BOOST_PEAK, boost_display_func);
    register_display_mode(MODE_BOOST_INSTANT, boost_display_func);
    register_display_mode(MODE_ATMOSPHERIC, boost_display_func);

    register_timer_callback(&boost_timer, BOOST_UPDATE_PERIOD,
            boost_update_callback, 0);
}

/******************************************************************************
* boost_gauge_adc_callback
*        
*******************************************************************************/
void boost_gauge_adc_callback(adc_context_t *ctx)
{
    boost_context.sample_accumulator += ctx->sample;
    ++boost_context.sample_counter;
    if (boost_context.sample_counter == SAMPLES_PER_AVG)
    {
        log_data_sample(DATA_TYPE_MAP_VOLTS, 2, (u8*)&boost_context.sample_accumulator);

        boost_context.sample_counter = 0;
        boost_context.current_boost_accum = boost_context.sample_accumulator;
        boost_context.boost_accum_valid = 1;
        boost_context.sample_accumulator = 0;

        if (boost_context.current_boost_accum > boost_context.peak_boost_accum
                || boost_context.peak_counter > PEAK_AGE)
        {
            boost_context.peak_boost_accum = boost_context.current_boost_accum;
            boost_context.peak_counter = 0;
        }
    }
}

u16 boost_read_raw_adc()
{
    return boost_context.current_boost_accum;
}

void boost_update_callback(timerentry_t *ctx)
{
    if (boost_context.peak_counter <= PEAK_AGE)
    {
        ++boost_context.peak_counter;
    }
    
    register_timer_callback(&boost_timer, BOOST_UPDATE_PERIOD,
            boost_update_callback, 0);    
}

u16 adjust_map(u16 avg_adc10q6)
{
    /* This sanity check costs 26 instructions.
     * It should never happen and could be removed...
     */
    s16 adj = map_constants[boost_context.map_sensor_idx].adc_offset_10q6;
    if (adj < 0 && avg_adc10q6 < -adj)
    {
        return 0;
    }
    else /* Note: this could overflow! */
    {
        u32 t = (u32)avg_adc10q6 + adj;
        if (t > 0xFFFF)
        {
            return 0xFFFF;
        }            
        else
        {
            return t;
        }
    }
}   

s8 boost_get_psig()
{
    u16 map = adjust_map(boost_context.current_boost_accum);

    if (map >= boost_context.atmospheric10q6)
    {
        return (map-boost_context.atmospheric10q6)/CODE_PSI_10Q6;
    }
    else
    {
        return -((boost_context.atmospheric10q6-map)/CODE_PSI_10Q6);
    }
}


u8 boost_display_func(ui_mode_t mode, ui_display_event_t event)
{
    if (event >= IN_CONFIG)
        return boost_config_func(mode, event);

    u8 ignore_radio = 0;
    boost_context.blink_counter = (boost_context.blink_counter+1)%BLINK_WRAP;
    
    if (event == DISPLAY_MODE_JUST_CHANGED)
    {
        boost_context.mode_change_downcounter = 20; 
    }

    if (boost_context.mode_change_downcounter)
    {
        ignore_radio = 1;
        --boost_context.mode_change_downcounter;
    }
    
    if (boost_context.boost_accum_valid)
    {
        u8 peak_is_boost = adjust_map(boost_context.peak_boost_accum) >= 
                           (boost_context.atmospheric10q6 + (1*CODE_PSI_10Q6/10));
        
        if (mode == MODE_BOOST_PEAK && peak_is_boost)
        {
            output_boost(boost_context.peak_boost_accum, mode, 1, ignore_radio);
        }
        else if (mode == MODE_ATMOSPHERIC)
        {
            output_boost(boost_context.atmospheric10q6, mode, 0, 1);
        }
        else 
        {
            output_boost(boost_context.current_boost_accum, mode, 0, ignore_radio);
        }
    }
    return 0;
}
u16 adc_to_mbar(u16 avg_adc10q6)
{
    u32 mbar = ((((u32)avg_adc10q6<<4) / DIV_1BAR_16Q0)*1000)>>10;

    return mbar;
}

u16 boost_get_mbar()
{
    return adc_to_mbar(adjust_map(boost_context.current_boost_accum));
}


/* Note: multiple division functions are called out from here.
 * To reduce code space, change casting.
 *
 *      __udivmodhi4    : 16 / 16
 *      __udivmodsi4    : 32 / 32 
 */

void output_boost(u16 avg_adc10q6, u8 mode, u8 is_peak, u8 ignore_radio)
{
    u8 override_radio = ignore_radio;

    center_letter_t         center = CENTER_NONE;
    number_display_flags_t  dflags = 0;
    s16                     value;
    
#ifdef RADIO_IN_SUPPORT
    if (mode == MODE_BOOST_INSTANT && 
        boost_context.output_mode == OUTPUT_MODE_PSIG_ONLY_RADIO)
    {
    	override_radio = 1;
    }
#endif

    if (boost_context.atmospheric_status == ATMOS_DEFAULT)
    {
    	boost_context.atmospheric10q6 = STD_ATMOSPHERE_10Q6;
    }
    
    if (mode != MODE_ATMOSPHERIC)
    {
        avg_adc10q6 = adjust_map(avg_adc10q6);
    }

    if (mode == MODE_ATMOSPHERIC)
    {
        if (boost_context.atmospheric_status == ATMOS_MEASURED)
        {
            center = CENTER_A;
            dflags |= CENTER_FLASH;
        }
        else if (boost_context.atmospheric_status == ATMOS_LOADED)
        {
            center = CENTER_A;
        }
        else if (boost_context.atmospheric_status == ATMOS_DEFAULT)
        {
            center = CENTER_D;
        }
        else
        {
            center = CENTER_E;
            dflags |= CENTER_FLASH;
        }
    }
    
    if (is_peak)
    {
        dflags |= CENTER_FLASH;
    }
            
    if (avg_adc10q6 > boost_context.atmospheric10q6)
    {
        center = CENTER_B;
    }
        
    if (boost_context.output_mode == OUTPUT_MODE_ABSOLUTE || mode == MODE_ATMOSPHERIC)
    {
        // 10Q6 -> 22Q10 / 16Q0 = 22Q10

        value = adc_to_mbar(avg_adc10q6);
#if 0
        send_msg(BROADCAST_NODE_ID, 0xCC, 4, &mbar, 0); 
        send_msg(BROADCAST_NODE_ID, 0xCD, 2, &avg_adc10q6, 0); 
        send_msg(BROADCAST_NODE_ID, 0xCE, 2, &DIV_1BAR_16Q0, 0); 
#endif
    }
    else
    {
        /* non-metric
         * when mode == MODE_ATMOSPHERIC, show in inHg
         * else if < saved atmospheric, subtract from atmospheric
         * and show in inHg
         * else subtract atmospheric and show in PSI
         *
         * Note that when displaying atmospheric pressure,
         * avg_adc10q6 will == atmospheric10q6 
         */
        
        if (avg_adc10q6 <= boost_context.atmospheric10q6)
        {
            /* vacuum.  
             *
             *    1 bar = 29.53 inHg at STP = 0 vacuum
             *   .5 bar = 14.77 inHg at STP = 14.8 inHg vacuum
             *   
             *   bar (10q6) = adc / 64 =
             *   mbar       = adc * 15.625  (1000/64)
             *
             * */

            /* doing a 10q6 / 14q2 = 12q4 division:
             *  The error here gives 29.49 @ 1 bar (vs. 29.53)
             *  u16 inHg12q4 = avg_adc10q6 / CODE_INHG_14Q2;
             *
             * doing a 20q12 / 10q6 = 10q6 division: 
             *  The error with the 32/16 division gives
             *  29.53 @ 1 bar
             */
            
            u32 adc20q12;
            
            // vacuum 0 = atmospheric
            adc20q12 = 
                ((u32)(boost_context.atmospheric10q6 - avg_adc10q6))<<6;

            /* convert adc10q6 to inHg ... */

            u16 inHg10q6 = adc20q12 / CODE_INHG_10Q6;
            u16 tenths = (inHg10q6*10)>>6;
        
            center = CENTER_NONE;
            dflags = DFLAGS_NONE;
            value = tenths;
        }           
        else
        {
            /* Boost.  mode != ATMOSPHERIC 
             *
             *  Convert to PSI
             * 
             * */
            u16 adcgauge10q6 = 
                (avg_adc10q6 - boost_context.atmospheric10q6);
            
            /* 20q12 / 10q6 = 10q6 */
            u16 psig10q6 = ((u32)adcgauge10q6<<6) / CODE_PSI_10Q6;
            u16 tens = ((psig10q6)*10)>>6;

            override_radio = 1;
            
            if (tens == 0 && !is_peak)
            {
                /* Set 'B' except when hovering around zero PSIG */
                center = CENTER_NONE;
            }            
            value = tens;
        }
    }

#ifdef RADIO_IN_SUPPORT
    if (!override_radio)
    {    
        u8 radio_msg[7];
        u8 i;
        /* If the conditions above (boost) are not met for overriding
         * the radio, copy the current radio message to the output 
         * buffer. */
        if (boost_context.output_mode == OUTPUT_MODE_PSIG_ONLY_RADIO)
        {
            if (!is_valid)
            {
                /* Blank the screen */
                for(i=0;i<7;i++)
                {
                    radio_msg[i] = blank_screen_msg[i];
                }
            }
            else
            {
                u8 flags = disable_interrupts();
                for(i=0;i<7;i++)
                {
                    radio_msg[i] = last_valid_msg[i];
                }
                restore_flags(flags);
            }
            send_to_task(TASK_ID_RADIO_OUTPUT, RADIO_MSG_SEND, 7, radio_msg);
            return;
        }
    }
#endif
    /* Never reached if radio override */
    output_number(mode, value, center, dflags);
}

extern u8 cluster_is_1995;

/* Return zero to exit, nonzero to remain in config mode. */
static inline u8 boost_config_func(ui_mode_t mode, ui_display_event_t event)
{
    if (mode == MODE_ATMOSPHERIC)
    {
	    boost_context.configure_mode = CONFIG_MAP;
    }
    else
    {
	    boost_context.configure_mode = CONFIG_UNITS;
    }
    
    if (event == CONFIG_SHORT_PRESS)
    {
        if (boost_context.configure_mode == CONFIG_UNITS)
        {
            boost_context.output_mode++;
#ifdef RADIO_IN_SUPPORT
            if (cluster_is_1995 && boost_context.output_mode == OUTPUT_MODE_PSIG_ONLY_RADIO)
            {
                boost_context.output_mode++;
            }
#endif
            if (boost_context.output_mode >= OUTPUT_MODE_INVALID)
            {
                boost_context.output_mode = 0;
            }
            save_persist_data(PDATA_MAP_UNITS, boost_context.output_mode);
        }
        else
        {
            ++boost_context.map_sensor_idx;
            if (boost_context.map_sensor_idx >= 
                    sizeof(map_constants)/sizeof(map_sensor_constants_t))
            {
                boost_context.map_sensor_idx = 0;
            }
            save_persist_data(PDATA_MAP_SENSOR_INDEX, boost_context.map_sensor_idx);
            /* Reset atmospheric measurement */
            boost_context.atmospheric_status = ATMOS_DEFAULT;
            save_persist_data_16(PDATA_ATMOSPHERIC, 0xFFFF);            
        }            
    }
    else if (event == CONFIG_LONG_PRESS)
    {
        return 0;
    }
    
    if (boost_context.configure_mode == CONFIG_UNITS)
    {
#ifdef RADIO_IN_SUPPORT
        if (boost_context.output_mode == OUTPUT_MODE_PSIG_ONLY_RADIO)
        {
            radioin_display_func(mode, DISPLAY_MODE_JUST_CHANGED);
        }
        else
#endif
        {
            output_boost(64*1023L, mode, 0, 1);
        }
    }
    else
    {
        output_number(mode, map_constants[boost_context.map_sensor_idx].maxbars_x10,
                CENTER_NONE, DFLAGS_NONE);
    }

    return 1; // not done
}
            

/*
 *      Measuring atmospheric pressure
 * 
 *      Note: pressure ratios at altitudes:
 *      11900 ft: ??????    (La Paz, Boliva)
 *      10000 ft: 0.6877    (Leadville, CO)
 *      8000  ft: 0.7428
 *      7000  ft: 0.7716    (Evergreen, CO)
 *      6000  ft: 0.8014    (Colorado Springs, CO)
 *      5000  ft: 0.8320
 *      4000  ft: 0.8637    (Kansas)
 *      3000  ft: 0.8962
 *      1000  ft: 0.9644
 *      0     ft: 1.0000
 *
 *  Method 1:
 *      At power-on, wait 1 second for MAP output to stabilize.
 *      Monitor MAP for 2 seconds.
 *      If it does not deviate +/- 10 (50?) mbar, and the voltmeter
 *      reads below 12.5 volts, and does not dip below 11.5,
 *      assume the engine is off and and not cranking, and
 *      prompt the user to measure atmospheric
 *      pressure by displaying the pressure in millibars and
 *      flashing "A" symbol for 5 seconds.  If during that 
 *      time the user presses the button, then store the 
 *      measured atmospheric pressure in the EEPROM.  Otherwise
 *      exit the atmospheric measurement mode.
 *
 *      If the above mode is not entered, or the user does
 *      not press the button within the timeout period, then
 *      load the stored atmospheric pressure value from the
 *      EEPROM.  If it is not present, default to 1013 mbar 
 *      (14.7 PSI).
 */


void boost_load_atmospheric()
{
    boost_context.atmospheric10q6 = load_persist_data_16(PDATA_ATMOSPHERIC);
    u16 map = adjust_map(boost_context.atmospheric10q6);
    
    /* If value in EEPROM is not realistic, use standard atmosphere. */
    if (map > ATMOSPHERIC_MAX_SANE_10Q6 ||
        map < ATMOSPHERIC_MIN_SANE_10Q6)
    {
        boost_context.atmospheric10q6 = STD_ATMOSPHERE_10Q6;
        boost_context.atmospheric_status = ATMOS_DEFAULT;
    }
    else
    {
        boost_context.atmospheric_status = ATMOS_LOADED;
    }
}

void boost_store_atmospheric()
{
    if (boost_context.boost_accum_valid &&
        boost_context.current_boost_accum < ATMOSPHERIC_MAX_SANE_10Q6 &&
        boost_context.current_boost_accum > ATMOSPHERIC_MIN_SANE_10Q6)
    {        
        boost_context.atmospheric10q6 = boost_context.current_boost_accum + 
		map_constants[boost_context.map_sensor_idx].adc_offset_10q6;

        /* Save persistent 16 bit value */
        save_persist_data_16(PDATA_ATMOSPHERIC, boost_context.atmospheric10q6);
        boost_context.atmospheric_status = ATMOS_MEASURED;
    }
    else
    {
        boost_load_atmospheric();
        boost_context.atmospheric_status = ATMOS_ERROR;
    }
}
 

/* 3 things:
 *  MM instead of MV
 *  Radio-off / not-valid not set until first message from radio arrives (just MMs instead)
 *  B (boost) / A B C is confusing
 */

