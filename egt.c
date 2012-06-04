/******************************************************************************
* File:              egt.c
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

#include "timers.h"
#include "persist.h"
#include "userinterface.h"
#include "egt.h"
#include "output.h"
#include "onewire.h"
#include "comms_generic.h"
#include "datalogger.h"

/* Calibration:
 *  Pick two calibration points.  Maybe 1000F and 1500F.
 *  Stimulate with thermometer calibrator at those two points.
 *  Determine the mV offset which would minimize the error at the cal points.
 *  Write that mV offset to register 0x33 in units of 15.625uV.
 */

/* Auxillary VIN for WB02
 * Registers 0xC and 0xD (MSB:LSB) in units of 4.88mV
 * Range is 0-4.75V but scales as 0-5V (i.e. 1 LSB = 5/1024 V
 *
 * So V = VINREG * 5 / 1024
 *
 * The default LC1 range sets AFR 7.35 = 0V and AFR 22.39 = 5V.
 * A better range would be 8 - 20 AFR from 0-4V.
 * This would mean that AFR = 3 * VINREG * 5 / 1024 + 8
 *
 *
 * If the LC-1 is set up to output 4.65 @ warmup and 4.75 at error
 *
 *
 * 0-4.0V  7.5 - 22.0 AFR
 *
 * LC1 values:
 * Error 4.5V Warmup 4.0V
 * 0V at AFR 8
 * 3.418V at AFR 22 (21.99)
 *
 * AFR 8-22 = 14*50 codes (0-699)
 *
 * if (code > 870) // 4.25V
 *    error
 * else if (code > 768) // 3.75V
 *    warmup
 * else
 *    AFR = code / 14  (0-
 *
 * This is pretty good:
 *   Use codes 0-840 for AFR-8 of 0-14  (8-22 AFR)
 *   0V @ AFR 8, 4.102V @ AFR 22
 *   Warmup 4.3 Error 4.6
 *    147 = 80 + code/6    (code = 403)
 *
 * LC-1 factory defaults for analog 2:  0V @ 7.35AFR 5V @ 22.39AFR
 *
 *  0.00V @ 7.35 AFR  (lamda 0.5)
 *  4.00V @ 22.05 AFR (lamda 1.5)
 *
 *  AFR = 7.35 + (code/819)*14.7
 *  Lambda = code/819
 *  dispAFR = 74 + code/82 * 15    Code@14.7 = 412  dispAFR = 149  (bleh)
 *
 */


u8 egt_display_func(ui_mode_t mode, ui_display_event_t event);
u8 wideband_display_func(ui_mode_t mode, ui_display_event_t event);

static inline u8  egt_config_func(ui_mode_t mode, ui_display_event_t event);
void egt_update_callback(timerentry_t *ctx);
s16  convert_thermocouple_volts_to_temp(thermocouple_raw_data_t *tc);

#define EGT_UPDATE_PERIOD MS_TO_TICK(250)

#define DEFAULT_PEAK_AGE                4*5 /* in units of UPDATE_MS */
#define LONG_PEAK_AGE                   4*10

extern u8 peak_hold_time_10s;
#define EGT_PEAK_AGE (peak_hold_time_10s?LONG_PEAK_AGE:DEFAULT_PEAK_AGE)
    
timerentry_t            egt_update_timer;
thermocouple_raw_data_t tc_data;
u8                      tc_status;
u8                      tc_units_fahrenheit;
u8                      egt_peak_counter;
s16                     PeakTempC;

u8                      tc_module_blink_counter;

void egt_init()
{
    tc_units_fahrenheit = load_persist_data(PDATA_EGT_UNITS);
    
    register_display_mode(MODE_EGT_INSTANT, egt_display_func);
    register_display_mode(MODE_EGT_PEAK, egt_display_func);
    register_display_mode(MODE_WIDEBAND, wideband_display_func);

    egt_update_callback(0);
}


void egt_update_callback(timerentry_t *ctx)
{
    if (egt_peak_counter < EGT_PEAK_AGE)
    {
        ++egt_peak_counter;
    }

#if 0
    if (tc_module_blink_counter < 32)
    {
        ++tc_module_blink_counter;

        u8 on = (tc_module_blink_counter&3)?0:0xFF;
        ow_2760_write_reg(8, on);
    }
#endif

    register_timer_callback(&egt_update_timer, EGT_UPDATE_PERIOD, 
            egt_update_callback, 0);
}

    
u8 egt_display_func(ui_mode_t mode, ui_display_event_t event)
{
    s16 TempC;
    temp_format_mode_t dmode = TEMP_RANGE_1000;

    if (event >= IN_CONFIG)
        return egt_config_func(mode, event);
    
    if (tc_status == 0)
    {
        TempC = convert_thermocouple_volts_to_temp(&tc_data);

#if 0
        send_msg(BROADCAST_NODE_ID, 0xCA, 4, (u8 *)&tc_data); 
        send_msg(BROADCAST_NODE_ID, 0xCB, 2, (u8 *)&TempC); 
#endif   
        if (mode == MODE_EGT_PEAK)
        {            
            if (TempC > PeakTempC)
            {
                PeakTempC = TempC;
                egt_peak_counter = 0;
            }
            
            if (egt_peak_counter < EGT_PEAK_AGE)
            {                
                dmode |= TEMP_PEAK_HOLD;
                TempC = PeakTempC;
            }
            else
            {
                PeakTempC = TempC;
            }
        }
    }
    else
    {
        TempC = tc_status;
        dmode |= TEMP_SHOW_ERROR;
    }

    if ((s8)tc_data.thermocouple_volts_msb_lsb[0] < 0 || tc_status != 0)
    {
        /* Restart blinking */
        if (tc_module_blink_counter >= 32)
        {
            tc_module_blink_counter = 0;
        }
    }
    if (tc_units_fahrenheit)
    {
        dmode |= TEMP_FAHRENHEIT;
    }

    output_temperature(mode, TempC, dmode);
    return 0;
}
    
static inline u8 egt_config_func(ui_mode_t mode, ui_display_event_t event)
{
    if (event == CONFIG_SHORT_PRESS)
    {
        tc_units_fahrenheit = 
            !tc_units_fahrenheit;
        save_persist_data(PDATA_IAT_UNITS, tc_units_fahrenheit);
    }
    else if (event == CONFIG_LONG_PRESS)
    {
        /* done */
        return 0;
    }

    egt_display_func(mode, 0);

    /* not done */
    return 1;
}

s16 ds2760_vin;


/*
 * * This is pretty good:
 *   Use codes 0-840 for AFR-8 of 0-14  (8-22 AFR)
 *   0V @ AFR 8, 4.102V @ AFR 22
 *   Warmup 4.4 Error 4.6
 *    147 = 80 + code/6    (code = 403)
 */
#define VIN_ERROR  922  /* 4.5V */
#define VIN_WARMUP 880  /* 4.3V */
u8 wideband_display_func(ui_mode_t mode, ui_display_event_t event)
{
    number_display_flags_t  dflags = CENTER_FLASH;
    center_letter_t         cflags = CENTER_E;
    s16 number = 0;

#if 0
    send_msg(BROADCAST_NODE_ID, 0x34, 2, &ds2760_vin);
#endif

    if (tc_status != 0)
    {
        number = tc_status;
    }
    else if (ds2760_vin > VIN_WARMUP)
    {        
        number = 0;
        if (ds2760_vin < VIN_ERROR)
            cflags = CENTER_C;
    }
    else 
    {
        dflags = DFLAGS_NONE;        
        cflags = CENTER_NONE;
        if (ds2760_vin < 0)
            ds2760_vin = 0;
        else if (ds2760_vin%6 >= 3)
            ds2760_vin += 3;
        number = 80 + (ds2760_vin/6);
    }
    output_number(mode, number, cflags, dflags);

    return 0;
}
    
/* Vin calibration:
 * Unit E shows -13 to -15 at GND  989 at 5V  925 at 4.51V
 * Unit D shows -11 to -13 at GND             926 at 4.51V
 */

void egt_read_thermocouple()
{
    tc_status = ow_2760_read_reg(OW_2760_REG_TEMP_MSB, tc_data.cold_junction_temp_msb_lsb, 2);
    tc_status <<= 2;
    tc_status |= ow_2760_read_reg(OW_2760_REG_CURRENT_MSB, tc_data.thermocouple_volts_msb_lsb, 2);

#if 0
    if (raw_ret->thermocouple_volts_msb_lsb[0] > 0x7F)
    {
        static u8 blink;
        ow_2760_write_reg(8, blink);
        blink=~blink;
    }
#endif

    tc_status <<= 2;
    u8 vin[2];
    tc_status |= ow_2760_read_reg(OW_2760_REG_VOLTS_MSB, vin, 2);

//    send_msg(BROADCAST_NODE_ID, 0x35, 2, vin);

    /* Example with Vin = 2.500 on board F (something wrong with this one):
     * MSB = 0x59 LSB = 0x70    01011001 01110000
     *                          S9876543 210XXXXX  = code 715 = 3.48V
     * Expected:
     * MSB = 0x40 LSB = 0x00    01000000 00000000  
     *
     *
     * On a different board:
     * MSB = 0x3F LSB = 0xE0   = code 511
     */

    //ds2760_vin = ((vin[0]<<3)|(vin[1]>>5));
    ds2760_vin = ((vin[0]<<8)|vin[1]);
    ds2760_vin >>= 5;

    if (tc_status == 0)
    {
        log_data_sample(DATA_TYPE_EGT_RAW, sizeof(tc_data), (u8*)&tc_data);
        log_data_sample(DATA_TYPE_WB_VOLTS, 2, vin);
    }
}
    
/* First degree polynomial for type K thermocouples:
 *  temp = 5 + mvolts*24
 *
 *  This is accurate to +/- 5 degrees celsius from 0-1000C
 *
 *  The voltage read from the DS2760 is in units of 15.625 microvolts per LSB.
 *  The 2760 averages 128 samples and the averaged value is available every 88ms.
 *  
 *  In this simplified compensation scheme, the cold junction temperature is 
 *  added to the computed thermocouple temperature after the thermocouple is
 *  linearized.  For maximim accuracy it should be done by converting the
 *  CJ temp to a thermocouple voltage and adjusting the TC voltage before 
 *  the linearization.  But this is good enough.  The TDS-2 should use 
 *  the 8th degree poly and the better CJ compensation using the raw data
 *  which is logged instead of the computed temperature.
 *
 *  Since the DS2760 reads in units of 1/64th of a millivolt, the
 *  actual calculation used is:
 *
 *  TempC = 5 + 3*ds2760_val/8
 */
#define THERMOCOUPLE_OFFSET 5

/******************************************************************************
* convert_thermocouple_volts_to_temp
*        This returns a positive temperature in degrees celsius.
*        Negative temperatures are returned as zero.
*        Cold junction compensation is performed.
*******************************************************************************/
s16 convert_thermocouple_volts_to_temp(thermocouple_raw_data_t *tc)
{
    s16 TempC;    
    u8 msb = tc->thermocouple_volts_msb_lsb[0];
    
    if (msb&0x80)
    {
        /* Thermocouple is colder than "cold" junction */
        TempC = 0;
    }
    else
    {
        TempC = (msb<<5) + (tc->thermocouple_volts_msb_lsb[1]>>3);

        //TempC = TempC + TempC + TempC;
        TempC *= 3;     /* Probably already using 16 bit multiply, right? */
        TempC >>= 3;
        TempC += THERMOCOUPLE_OFFSET;
    }

    
    /* Cold junction comp.
     * MSB of temperature register is in degrees C */
    TempC += (s8)tc->cold_junction_temp_msb_lsb[0];

    return TempC;
}

