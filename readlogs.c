/******************************************************************************
* File:              readlogs.c
* Author:            Kevin Day
* Date:              March, 2007
* Description:       
*                    
*                    
* Copyright (c) 2007 Kevin Day
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


#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

#include "comms_generic.h"

char *serial_device = "/dev/ttyUSB0";
int port_speed = B115200;

int serial_fd = -1;
int open_serial()
{
    serial_fd = open(serial_device, O_RDWR|O_NOCTTY|O_NDELAY);
    if (serial_fd == -1)
    {
        perror("open_serial(): failed to open -");
    }
    else
    {
//        fcntl(serial_fd, F_SETFL, FNDELAY);
    }
    return serial_fd;
}
    
void config_port()
{
    struct termios options;
    tcgetattr(serial_fd, &options);
    cfsetispeed(&options, port_speed);
    cfsetospeed(&options, port_speed);
    
    options.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    options.c_cflag |= (CLOCAL|CREAD | CS8);

    options.c_lflag &= ~(ICANON|ECHO|ECHOE|ISIG);

    options.c_iflag &= ~(IXON|IXOFF|IXANY);

    options.c_oflag &= ~(OPOST);
    
    /* block for up to 1/10 sec on read */
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 1;
    
    tcsetattr(serial_fd, TCSANOW, &options);
    
}

u8 node_id = 0;

u8 get_node_id()
{
    return node_id;
}

void tx_enqueue(u8 data)
{
    int ret;
  again:
    ret = write(serial_fd, &data, 1);
    
    if (ret == 0)
    {
        fprintf(stderr, "TX overflow\n");
        goto again;
    }
    else if (ret == -1)
    {
        perror("tx_enqueue: ");
    }
}

int main(int argc, char *argv[])
{
    char ch;
    testmode_t mode = CONSOLE;
    FILE *fp;

    while ((ch=getopt(argc, argv, "p:r:t:b:v")) != -1)
    {
        switch(ch)
        {
            case 'p':
                serial_device = strdup(optarg);
                break;
            case 'b':
                if (!strcmp(optarg, "115200"))
                {
                    port_speed = B115200;
                }
                else if (!strcmp(optarg, "38400"))
                {
                    port_speed = B38400;
                }
                fprintf(stderr, "Using %s bps\n", optarg);
                break;
            default:
                fprintf(stderr, "Unknown option %c\n", ch);
                return -1;
        }
    }

    open_serial();
    config_port();


}

static inline void addr_to_buf(u32 addr, u8 *buf)
{
    buf[0] = addr>>16;
    buf[1] = addr>>8;
    buf[2] = addr;
}


    
typedef enum {
    READ_DESCRIPTORS,
    READ_SAMPLES,
} state_t;

u32 waitaddr;
u32 descriptor;
state_t state;

void read_page(u32 pagestart)
{
    u8 buf[3];
    addr_to_buf(pagestart, buf);
    waitaddr = pagestart;
    send_msg(0xF, 0x50|FLASH_CMD_READ_PAGE, 3, buf);
}

void process_descriptors(u32 addr, u16 length, u8 *buf)
{
}


void process_page(unsigned length, u8 *buf)
{
    switch(state)
    {
        case READ_DESCRIPTORS
            process_descriptors(waitaddr, length, buf);
            break;
        case READ_SAMPLES:
            process_samples(waitaddr, length, buf);
            break;
        default:
            fprintf(stderr, "bad state %d\n", state);
    }
}


void packet_received(msgaddr_t addr, u8 code, u16 length, u8 flags, u8 *payload)
{
    if (code == 0x50|FLASH_CMD_READ_PAGE)
    {
        process_page(length, payload);
    }
    else
    {
        fprintf(stderr, "** Packet code %02X not expected\n", code);
    }
}


