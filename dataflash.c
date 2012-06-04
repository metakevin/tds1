/******************************************************************************
* File:              dataflash.c
* Date:              November, 2005
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

#include "dataflash.h"

/* this is actually the V1 mute fet on rev B. */
#define DF_PORT PORTB
#define DF_DIR  DDRB
#define DF_PIN  1

#if 0
/* Is this really necessary? */
u8 df_mutex_flag;
static u8 df_mutex_enter()
{
    u8 flags = disable_interrupts();
    u8 r;
    if (!df_mutex_flag)
    {
        df_mutex_flag = 1;
        r = 0;
    }
    else
    {
        r = 1;
    }
    restore_flags(flags);
    return r;
}

static void df_mutex_exit()
{
    df_mutex_flag = 0;
}
#else
static inline u8 df_mutex_enter()
{
    return 0;
}
static inline void df_mutex_exit()
{
}
#endif


void dataflash_init()
{
    /* Set MOSI, SCK, FLASH_CS as outputs.
     * Also make sure /SS is an output.
     * This will interfere with the user of the SS pin, 
     * but in this case that's the onewire fet, which 
     * is always an output anyway.  */
    DDRB |= (1<<5) | (1<<3) | (1<<2) | (1<<1);  /* DF_PIN */

    DF_PORT |= (1<<DF_PIN); /* not selected */
    
    /* master init */
    /* SPIE = 0  - interrupt disabled
     * SPE  = 1  - SPI enabled
     * DORD = 0  - MSB first
     * MSTR = 1  - master mode
     * CPOL = 1  - spi mode 3
     * CPHA = 1  - spi mode 3
     * SPR1 = 0  - 01 = Fosc/16 or Fosc/8
     * SPR0 = 1  - "
     */
      SPCR = (1<<SPE)|(1<<MSTR)|(1<<SPR0)|(1<<CPOL)|(1<<CPHA);

//    SPCR = (1<<SPE)|(1<<MSTR)|(1<<SPR0)|(1<<SPR1)|(1<<CPOL)|(1<<CPHA);
    
    /* SPI2X = 1 - 2x mode */
//    SPSR = (1<<SPI2X);
}

static void spi_write(u8 byte)
{
    SPDR = byte;
    while (!(SPSR & (1<<SPIF)))
        ;
}

static u8 spi_read()
{
    while (!(SPSR & (1<<SPIF)))
        ;

    return SPDR;
}

static void df_select()
{
    DF_PORT &= ~(1<<DF_PIN);
}
static void df_deselect()
{
    DF_PORT |= (1<<DF_PIN);
}

/* 7: RDY
 * 6: COMP
 * 5:2 0111
 * 1:0 XX
 */

u8 dataflash_read_status_reg()
{
    /* opcode 0x57 */
    df_select();
    spi_write(STATUS_REGISTER);
    spi_read();
    spi_write(0);   /* just to toggle clock */
    df_deselect();
    
    return spi_read();
}
    
static void wait_for_ready()
{
    while (!(dataflash_read_status_reg()&0x80))
        ;
}

// delete this
void dataflash_buffer_write(u8 byte, u8 addr)
{
    df_select();
    spi_write(0x84);
    spi_write(0);
    spi_write(0);
    spi_write(addr);
    spi_write(byte);
    df_deselect();
}

// delete this
u8 dataflash_buffer_read(u8 addr)
{
    u8 ret;
    df_select();
    spi_write(0x54);
    spi_write(0);
    spi_write(0);
    spi_write(addr);
    spi_write(0);
    spi_write(0);
    ret = spi_read();
    
    df_deselect();

    return ret;
}

void spi_write_addr(flash_offset_t addr)
{
    /* 24 bit address format:
     * 0000 PPPPPPPPPPP BBBBBBBBB 
     * 4rsv 11 page     9 byte
     * 
     * There are 9 bits to select the byte in the page, but there are only 264 bytes per page.
     * These upper 8 bytes will not be used.   
     *
     *  So offset 256 is 
     *   0000  00000000001 000000000
     *
     *  and offset 255 is
     *   0000  00000000000 011111111
     * 
     *  There are 2048 pages (11 bits) of 256 useful bytes each (8 bits) for a total of 19
     *  address bits.
     * 
     * */
    flash_offset_t page = addr>>8;

    spi_write(page>>7); /* bits 11:7 of page */
    spi_write(page<<1); /* bits 6:0 of page and a zero for the MSB of the 9 bit offset */
    spi_write(addr);    /* bits 7:0 of byte offset in page */
}

void dataflash_write(flash_offset_t byte_offset, u8 length, u8 *data)
{
    if (df_mutex_enter())
    {
        return;
    }
    
    u8 f = disable_interrupts();
    while (length > 0)
    {        
        wait_for_ready();

        /* Load the buffer from flash */
        df_select();
        spi_write(0x55);    /* main memory to buffer 2 transfer */
        spi_write_addr(byte_offset);
        df_deselect();

        wait_for_ready();

        /* Write until the end of this page, or length bytes,
         * whichever comes first. */
        df_select();
        spi_write(0x87);    /* buffer 2 write */
        spi_write_addr(byte_offset);

        do {
            spi_write(*data++);
            ++byte_offset;
            --length;
        } while (length && (byte_offset&0xFF));
        
        df_deselect();

        wait_for_ready();

        /* Write the buffer back */
        df_select();
        spi_write(0x86);    /* buffer 2 to main memory w/ erase */
        spi_write_addr(byte_offset-1);
        df_deselect();
    }

    restore_flags(f);
    df_mutex_exit();
}

void dataflash_erase_all()
{
    const flash_offset_t block_size = 256*8;
    u16 i;

    if (df_mutex_enter())
    {
        return;
    }

    
    for(i=0; i<256; i++)
    {
        flash_offset_t addr = block_size * i;
        
        wait_for_ready();
        df_select();
        spi_write(0x50);
        spi_write_addr(addr);
        df_deselect();
    }

    df_mutex_exit();
}

void buffer_byte_consumer(u8 byte, u16 index, u16 ctx)
{
    u8 *buf = (u8 *)ctx;
    buf[index] = byte;
}

void dataflash_read_range(flash_offset_t byte_offset, u16 length, u8 *read_buf)
{
    dataflash_read_range_to_consumer(byte_offset, length, 
            buffer_byte_consumer, (u16)read_buf);
}

void dataflash_read_range_to_consumer(flash_offset_t byte_offset, u16 length, 
        byte_consumer_func_t consumer_f, u16 consumer_ctx)
{
    u16 i;
    if (df_mutex_enter())
    {
        return;
    }
        
next_page:
    wait_for_ready();
    
    df_select();
    spi_write(0x68);
    spi_write_addr(byte_offset);
    spi_write(0);
    spi_write(0);
    spi_write(0);
    spi_write(0);
    
    for(i=0; i<length; i++)
    {
#if 0
        /* array reads use 264 byte pages... must recompute address OR we
         * could just do dummy reads here. */
        if ((byte_offset + i)>>8 != byte_offset>>8)
        {
            length -= i;
            byte_offset += i;
            df_deselect();
            goto next_page;
        }
#endif   
        spi_write(0);
        consumer_f(spi_read(), i, consumer_ctx);

        /* dummy reads */
        if (((byte_offset+i)&0xFF)==0xFF)
        {
            u8 z;
            for(z=0; z<8; z++)                
            {
                spi_write(0);
                spi_read();
            }
        }            
    }

    df_deselect();

    df_mutex_exit();
}


