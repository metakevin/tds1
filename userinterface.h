/******************************************************************************
* File:              userinterface.h
* Author:            Kevin Day
* Date:              March, 2005
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

#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include "types.h"
#include "tasks.h"

#define UI_MSG_STOP_UPDATES 0
#define UI_MSG_RESTART_UPDATES 1
#define UI_MSG_LONG_BUTTON_PRESS 2
#define UI_MSG_SHORT_BUTTON_PRESS 3

#define UI_MSG_READ_ADC 4

#define MAX_DISPLAY_MODES 0x21  // too big?

typedef enum {
    MODE_BOOST_INSTANT      = 0x01,       
    MODE_BOOST_PEAK         = 0x02,
    MODE_ATMOSPHERIC        = 0x03,
    MODE_IAT                = 0x04,
    MODE_IAT_PEAK           = 0x05,
    MODE_EGT_INSTANT        = 0x06,
    MODE_EGT_PEAK           = 0x07,         
    MODE_OILPRES            = 0x08,    
    MODE_WIDEBAND           = 0x09,           /* U9 */
    MODE_VOLTMETER          = 0x10,           /* V  */
    MODE_FP_ABSOLUTE        = 0x12,           /* V2 */
    MODE_FP_RELATIVE        = 0x13,           /* V3 */
    MODE_FP_RELATIVE_TROUGH = 0x14,           /* V4 */
    MODE_DATALOGGER         = 0x15,           /* V5 */
//    MODE_SPEEDOMETER        = 0x25,           /* V5 */
//    MODE_TPS                = 0x26,           /* V6 */
    MODE_RADIO              = 0x1F,           /* (can be any number)*/
    MODE_MODE_SELECT        = 0x20,
//    MODE_TACHOMETER         = 0x21,           /* M1 */
} ui_mode_t;

extern ui_mode_t ui_mode;

typedef enum {
    DISPLAY_NORMAL_UPDATE,
    DISPLAY_MODE_JUST_CHANGED,    
    IN_CONFIG, // pseudo-mode, used in comparisons
    CONFIG_SCREEN_ENTER,
    CONFIG_SHORT_PRESS,
    CONFIG_LONG_PRESS,
    CONFIG_REFRESH,
} ui_display_event_t;

typedef enum {
    MODE_DISABLED   =0,
    MODE_ENABLED    =1
} mode_flags_t;


/* Perhaps display and config should be one function with an additional
 * parameter.  This would reduce the size of the display_modes structure
 * substantially... 
 *
 * DONE: saved 108 bytes of program and 76 bytes of sram.
 * */
typedef u8 (*display_func_t)(ui_mode_t, ui_display_event_t);
    
typedef struct {
    display_func_t  displayfunc;
    mode_flags_t    flags;
} display_mode_t;

void register_display_mode(ui_mode_t mode, display_func_t dfunc);
    

task_t *ui_task_create(u8 start_mode);

/* don't use this directly. */
extern u8 ui_in_config_flag;

static inline u8 ui_in_config_mode()
{
    return ui_in_config_flag;
}


#endif /* !USERINTERFACE_H */
