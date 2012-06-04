/******************************************************************************
* File:              button.c
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


void debounce_button(button_context_t *bs, u8 val)
{
    u8 oldstate = bs->state;
    
    if (val)
    {
        ++bs->count;
        if (bs->count == 0)
        {
            bs->state = 1;
        }
    }
    else
    {
        --bs->count;
        if (bs->count == 127)
        {
            bs->state = 0;
        }
    }
    if (bs->state != oldstate)
    {
        (*bs->callback)(bs);
    }
}
