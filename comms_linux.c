/******************************************************************************
* File:              comms_linux.c
* Author:            Kevin Day
* Date:              December, 2004
* Description:       Framed serial protocol, host side (linux/cygwin)
*                    
*                    
* Copyright (c) 2004 Kevin Day
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
#include <ctype.h>
#include <errno.h>
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <readline/readline.h>
#include <readline/history.h>
#include "comms_generic.h"

char *serial_device = "/dev/ttyUSB0";
int port_speed = B115200;

int voltage_log = 0;

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

//    fprintf(stderr, "TX: %02X\n", data);
}

int ui_fd = 0;
int ui_quit = 0;

typedef enum {
    UNKNOWN,
    QUIT,
    SEND,
    SETID,
    RADIO,
    MUTE,
    SAMPLES,
    DESCRIPTORS,
    PSAMP,
} command_id_t;

typedef struct {
    const char  *name;
    command_id_t id;
    const char *usage;
} command_def_t;

command_def_t commands[] = {
    { "quit",   QUIT,   "" },
    { "send",   SEND,   "<to> <code> [payload1] [payload2] [...]" },
    { "radio",  RADIO,  "<octet 1> ... <octet 7>  (hex format presumed)" },
    { "mute",   MUTE,   "" },
    { "setid",  SETID,  "<new id>"},
    { "samples", SAMPLES, "<descriptor #>"},
    { "descriptors", DESCRIPTORS, "" },
    { "psamp",   PSAMP, "<descriptor> [file] [bin]" }
};

void ui_usage(command_id_t cmd)
{
    if (cmd == UNKNOWN)
    {
        int i;
        fprintf(stderr, "Valid commands:\n");
        for(i=0; i<sizeof(commands)/sizeof(command_def_t); i++)
        {
            fprintf(stderr, "  %s %s\n", commands[i].name, commands[i].usage);
        }
    }
    else
    {
        int i;
        for(i=0; i<sizeof(commands)/sizeof(command_def_t); i++)
        {
            if (commands[i].id == cmd)
            {
                fprintf(stderr, "  %s %s\n", commands[i].name, 
                        commands[i].usage);
            }
        }
    }
}


#include "datalogger.h"
typedef enum {
        CMD_READ_HEADERS_BEGIN,
        CMD_READ_HEADERS_CONTINUE,
        CMD_READ_SAMPLES_BEGIN,
        CMD_READ_SAMPLES_CONTINUE,
        CMD_READ_SAMPLES_ALL,
        EVT_FLASH_PACKET_RECEIVED,
        EVT_PACKET_ERROR,
} cmd_or_event_t;    

typedef enum {
        NONE,
        STATE_READ_HEADERS_WAIT,
        STATE_READ_HEADERS_DONE,
        STATE_READ_SAMPLES_WAIT,
        STATE_READ_SAMPLES_DONE,
        STATE_BAD_PACKET
} flash_sm_state_t;

flash_sm_state_t read_flash_sm(cmd_or_event_t event, unsigned length, u8 *data);

logged_data_descriptor_t descriptors[MAX_DESCRIPTORS];
u8 *sample_buffers[MAX_DESCRIPTORS];



command_id_t parse_command(char *cmdstr)
{
    int m = -1, nm = 0, nu = 0;
    int i;
    for(i=0; i<sizeof(commands)/sizeof(command_def_t); i++)
    {
        int n = 0;
        const char *c = commands[i].name;
        char *s = cmdstr;
        while(*s && tolower(*s) == tolower(*c))
        {
            ++s;
            ++c;
            ++n;
        }
        if (n > nm)
        {
            m = i;
            nm = n;
        }
    }
    if (m == -1)
    {
        return UNKNOWN;
    }
    return commands[m].id;
}   


unsigned packet_count;

void write_samples_to_file(unsigned index, char *filename, int binary);
unsigned descriptor_count;

void ui_callback(char *str)
{
    char *p, *q = str;
    char *argv[20];
    int argc=0;
    int isvalid = 1;
    command_id_t cmd;
    char *str_for_hist = strdup(str);
 
    if (!str || *str == 0)
    {
        return;
    }
    
    while ((p=strtok(q, " ")))
    {
        argv[argc++] = p;
        q = NULL;
    }
    cmd = parse_command(argv[0]);
    
    switch(cmd)
    {
        case SEND:
        {
            int to, code, plen;
            u8 payload[100];

            if (argc<3)
            {
                ui_usage(SEND);
            }
            else
            {
                int i;
                
                to   = strtoul(argv[1], NULL, 0);
                code = strtoul(argv[2], NULL, 0);
                
                for(plen=0, i=3; i<argc; plen++, i++)
                {
                    payload[plen] = strtoul(argv[i], NULL, 0);
                }
                fprintf(stderr, "send_msg(%X, %X, %X, ...)\n",
                        to, code, plen);
                send_msg(to, code, plen, payload);
                
            }
            break;
        }
        case RADIO:
        {
            if (argc != 8)
            {
                ui_usage(RADIO);
            }
            else
            {
                u8 radio_msg[7];
                int i;
                for(i=0; i<7; i++)
                {
                    radio_msg[i] = strtoul(argv[i+1], NULL, 16);
                }
                send_msg(0, 0x22, 7, radio_msg);
            }
            break;
        }
        case MUTE:
        {
            send_msg(0, 0x30, 0, 0);
            break;
        }
        case SETID:
            if (argc < 2)
            {
                ui_usage(SETID);
            }
            else
            {
                node_id = strtoul(argv[1], NULL, 0) & 0xF;
                fprintf(stderr, "New node id: %X\n", node_id);
            }
            break;
        case QUIT:
            ui_quit = 1;
            break;

        case SAMPLES:
            {
                unsigned index;
                if (argc != 2)
                {
                    ui_usage(SAMPLES);
                    break;
                }

                index = strtoul(argv[1], NULL, 0);
                if (strcmp(argv[1], "all"))
                {
                    read_flash_sm(CMD_READ_SAMPLES_BEGIN, sizeof(index), (u8 *)&index);
                }
                else
                {
                    read_flash_sm(CMD_READ_SAMPLES_ALL, 0, 0);
                }
        
                break;
            }
        case DESCRIPTORS:
            {
                read_flash_sm(CMD_READ_HEADERS_BEGIN, 0, 0);
                break;
            }

        case PSAMP:
            {
                unsigned index;
                char *filename = NULL;
                FILE *file;
                int bin = 0;
                
                if (argc != 3  && argc != 4) 
                {
                    ui_usage(PSAMP);
                    break;
                }
                if (argc == 4 && !strcmp(argv[3], "bin"))
                {
                    bin = 1;
                }

                if (argv[2] && strcmp(argv[2], ""))
                {
                    filename = argv[2];
                }
                
                if (strcmp(argv[1], "all"))
                { 
                    index = strtoul(argv[1], NULL, 0);
                    write_samples_to_file(index, filename, bin);
                }
                else
                {
                    for(index=0; index<descriptor_count; index++)
                    {
                        char buf[256];
                        sprintf(buf, "%s-%d.psamp", filename, index);
                        write_samples_to_file(index, buf, bin);
                    }
                }

                break;
            }
                
                
        default:
            isvalid = 0;
            ui_usage(UNKNOWN);
    }
    if (isvalid)
    {
        add_history(str_for_hist);
    }
    free(str_for_hist);
}

void init_ui()
{
    using_history();
    rl_callback_handler_install("avrtalk> ", ui_callback);        
}

void quit_ui()
{
    rl_callback_handler_remove();
    fprintf(stderr, "\n");
}
                    
void do_console()
{
    int nb,ret;
    char in;
    fd_set rdfds;
    
    init_ui();

    
    while (!ui_quit)
    {
        FD_ZERO(&rdfds);
        FD_SET(serial_fd, &rdfds);
        FD_SET(ui_fd, &rdfds);
        ret = select(serial_fd+1, &rdfds, NULL, NULL, NULL);
//        fprintf(stderr, "select returned %d\n", ret);
        if (ret == -1)
        {
            perror("select");
            return;
        }
        else if (ret == 0)
        {
            continue;
        }

        if (FD_ISSET(serial_fd, &rdfds))
        {
            nb=read(serial_fd, &in, 1);
            
            if (nb <= 0)
            {
                fprintf(stderr, "read(serial_fd,...): %d - %s (%d)\n",
                    nb, strerror(errno), errno);
            }            
            else
            {
//                fprintf(stderr, "IN: %02X\n", (u8)in);
                rx_notify(in, 0);
            }
        }
        if (FD_ISSET(ui_fd, &rdfds))
        {
            rl_callback_read_char();
        }
    }
    quit_ui();
}
            
void parsebyte(u8 b)
{
    static u8 msg[7];
    static u8 i;

    enum {MONO,STEREO} modulation;
    enum {AM, FM1, FM2} band;
    enum {RADIO,TAPE} mode;
    char station[10];
    unsigned preset;
    
    /* msg[0] is the oldest octet received */
    msg[0] = msg[1];
    msg[1] = msg[2];
    msg[2] = msg[3];
    msg[3] = msg[4];
    msg[4] = msg[5];
    msg[5] = msg[6];
    msg[6] = b;

    if ((msg[0] == 0x1A || msg[0] == 0x1E) && msg[1] != 0x40)
    {
        mode = RADIO;
        band = msg[0]==0x1A ? FM1 : FM2;
        if (msg[1] == 0x02)
        {
            modulation = STEREO;
        }
        else
        {
            modulation = MONO;
        }
        sprintf(station, "%c%d%d.%d", (msg[2]&0xF)==0?' ':'1', msg[3]&0xF, 
                msg[4]&0xF, msg[5]&0xF);
    }
    else if (msg[0] == 0x8A && msg[1] == 0x00)
    {
        mode = RADIO;
        band = AM;
        modulation = MONO;
        sprintf(station, "%d%d%d%d", msg[2]&0xF, msg[3]&0xF, msg[4]&0xF, msg[5]&0xF);
    }
    else if (msg[1] == 0x40)
    {
        mode = TAPE;
    }
    else
    {
        return;
    }

    preset = (msg[6] >> 4);
    
    printf("\nDecode: ");
    
    if (mode == TAPE)
    {
        printf("TAPE\n");
    }
    else if (mode == RADIO)
    {
        printf("Radio %s %s %s",
                band==FM1?"FM1":band==FM2?"FM2":"AM", modulation==STEREO?"STEREO":"MONO",
                station);
        if (preset)
        {
            printf(" Preset %d\n", preset);
        }
        else
        {
            printf("\n");
        }
    }
    else
    {
        printf("Unknown mode\n");
    }
}
    
char bdigit2txt(u8 digit)
{
    digit >>= 4;
    if (digit == 0)
    {
        return ' ';
    }
    if (digit <= 9)
    {
        return '0' + digit;
    }
    if (digit == 0xF)
    {
        return ' ';
    }
    return '?';
}
char digit2txt(u8 digit)
{
    digit &= 0xF;

    if (digit <= 9)
    {
        return '0' + digit;
    }
    if (digit == 0xF)
    {
        return ' ';
    }
    return '?';
}

int last, padding;

void packet_received(msgaddr_t addr, u8 code, u8 length, u8 flags, u8 *payload)
{

    unsigned i;
#if 0
    fprintf(stderr, "** packet_received (debug all)\n"
                    "from:   0x%X\n"
                    "  to:   0x%X\n"
                    "code:   0x%02X\n"
                    "length: %d\n"
                    "flags:  %X\n",
                    addr.from,
                    addr.to,
                    code,
                    length,
                    flags
                    );
    
    for(i=0; i<length; i++)
    {
        fprintf(stderr, "%02X: %02X\n", i, payload[i]);
    }
    fprintf(stderr, "-- end of packet\n");
#endif

#if 0
    fprintf(stderr, "RPKT: ");
    for(i=0; i<length; i++)
    {
        if (i%32==0)
        {
            fprintf(stderr, "%02X: ");
        }
        fprintf(stderr, "%02X", payload[i]);
        if (i%32==15)
        {
            fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, "\n");
#endif
    ++packet_count;

    if (voltage_log)
    {
        if (code == 0xCC)
        {
            fprintf(stderr, "Average ADC: %02X%02X\n", payload[1], payload[0]);
        }
    }
    else if (code == 0x34)
    {
        unsigned adc = (payload[1]<<8) | payload[0];

        double vin = (adc * 5.0) / (64*1024);
        double vinx = (adc * 5.0) / 1024;

        fprintf(stderr, "ADC = %5d Vin %1.3f or %1.3f\n", adc, vin, vinx);
    }
    else if (code == 0x35)
    {
        fprintf(stderr, "                                     "
                "ADC: %02X %02X\n", payload[0], payload[1]);
    }
    else if (code == 0xa0)
    {
        unsigned mphx10 = (payload[1]<<8) | payload[0];

        fprintf(stderr, "MPH: %d.%d\n", mphx10/10, mphx10%10);
    }
#if 0
    else if (code == 0x53 && (payload[0]&0xF)==0xA && length-6 == payload[0]>>4)
    {
        static unsigned last_time = 0;
        unsigned timestamp = payload[5]<<24 | payload[4]<<16 | payload[3]<<8 | payload[2];
        unsigned tdelt = timestamp - last_time;
        fprintf(stderr, "| %02X %02X %6d.%03d.%03d (+%d.%03d.%03d) ",
        //fprintf(stderr, "| %02X %02X %08X %02X%02X\n",
                payload[0], payload[1],
                timestamp/1000000, (timestamp/1000)%1000, timestamp%1000,
                tdelt/1000000, (tdelt/1000)%1000, tdelt%1000);
        int i;
        for(i=6; i<length; i++)
        {
            fprintf(stderr, "%02X ", payload[i]);
        }
        fprintf(stderr, "\n");
            
        last_time = timestamp;

    }
#else
    else if (code == 0x53 && read_flash_sm(EVT_FLASH_PACKET_RECEIVED, length, payload) != STATE_BAD_PACKET)
    {
        ;
    }
#endif
    else if (code == 0x55)
    {
        int i;
        
        for(i=0; i<length; i++)
        {
            fprintf(stderr,"%02X ", payload[i]);
            if (!last || i < padding/8)
            {
                parsebyte(payload[i]);
            }
        }
        if (last)
        {
            fprintf(stderr," [padding = %d]\n\n", padding);
            last = 0;
            padding = 0;
        }
    }
    else if (code == 0x77)
    {
        padding = payload[0];
        if (padding)
        {
            last = 1;
        }
        else
        {
            fprintf(stderr," [padding = 0]\n\n");
        }
    }
    else if (code == 0x99)
    {
        fprintf(stderr,"--overflow-- ");
    }
    else if (code == 0xEE && length == 2)
    {
        fprintf(stderr, "State: %d -> %d\n", 
                payload[0], payload[1]);
    }
    else if (code == 0xEE && length == 4)
    {
        fprintf(stderr, "State: %d -> %d (t = %d, bit = %d)\n", 
                payload[0], payload[1], payload[2], payload[3]);
    }
    else if (code == 0xDD && length == 7)
    {
        fprintf(stderr, "Radio message: %02X %02X %02X %02X %02X %02X %02X\n",
                payload[0], payload[1], payload[2], payload[3], payload[4], 
                payload[5], payload[6]);

        if (payload[0] == 0x1C)
        {
            fprintf(stderr, "%c%c%c.%c\n", 
                    digit2txt(payload[2]),
                    digit2txt(payload[3]),
                    digit2txt(payload[4]),
                    digit2txt(payload[5]));
            if (payload[6]&0xF)
            {
                fprintf(stderr, "  %c\n", (payload[6]&0xF)+'A'-1);
            }            
            else
            {
                fprintf(stderr, "\n");
            }
                        
            fprintf(stderr, " U%c\n", bdigit2txt(payload[6]));
            
            fprintf(stderr, "----\n");                    
        }
        if (payload[0] == 0xA0)
        {
            fprintf(stderr, "%c%c%c%c\n", 
                    digit2txt(payload[2]),
                    digit2txt(payload[3]),
                    digit2txt(payload[4]),
                    digit2txt(payload[5]));
            if (payload[6]&0xF)
            {
                fprintf(stderr, "  %c             (<95)\n", (payload[6]&0xF)+'A'-1);
            }            
            else
            {
                fprintf(stderr, "\n");
            }
                        
            fprintf(stderr, " M%c\n", bdigit2txt(payload[6]));
            
            fprintf(stderr, "----\n");                    
        }
        
        if (payload[0] == 0xB0)
        {
            fprintf(stderr, "%c%c%c.%c\n", 
                    digit2txt(payload[2]),
                    digit2txt(payload[3]),
                    digit2txt(payload[4]),
                    digit2txt(payload[5]));
            if (payload[6]&0xF)
            {
                fprintf(stderr, "  %c             (<95 ?)\n", (payload[6]&0xF)+'A'-1);
            }            
            else
            {
                fprintf(stderr, "\n");
            }
                        
            fprintf(stderr, " V%c\n", bdigit2txt(payload[6]));
            
            fprintf(stderr, "----\n");                    
        }
            
                    

    }
    else if (code == 0xE7 && length == 2)
    {
	fprintf(stderr, "## is_valid = %d isr_check = %d\n",
			payload[0], payload[1]);
    }
    else if (code == 0xE8 && length == 14)
    {
	fprintf(stderr, "## received bits = %02X %02X %02X %02X %02X %02X %02X\n"
			"                   %02X %02X %02X %02X %02X %02X %02X\n",
			payload[0],
			payload[1],
			payload[2],
			payload[3],
			payload[4],
			payload[5],
			payload[6],
			payload[7],
			payload[8],
			payload[9],
			payload[10],
			payload[11],
			payload[12],
			payload[13]);
    }
    else if (code == 0xE9 && length == 7)
    {
	fprintf(stderr, "## last valid    = %02X %02X %02X %02X %02X %02X %02X\n",
			payload[0],
			payload[1],
			payload[2],
			payload[3],
			payload[4],
			payload[5],
			payload[6]);
    }
    else
    {
        fprintf(stderr, "** packet_received\n"
                        "from:   0x%X\n"
                        "  to:   0x%X\n"
                        "code:   0x%02X\n"
                        "length: %d\n"
                        "flags:  %X\n",
                        addr.from,
                        addr.to,
                        code,
                        length,
                        flags
                        );
        
        for(i=0; i<length; i++)
        {
            fprintf(stderr, "%02X: %02X (%d)\n", i, payload[i], payload[i]);
        }
        fprintf(stderr, "-- end of packet\n");
    }
        
    fflush(stdout);
        
}

void bad_packet_received(msgaddr_t addr, u8 code, u8 length, u8 flags, u8 *payload)
{
    fprintf(stderr, "BAD packet received: %X%X %02X %02X %02X\n",
            addr.from,addr.to, code, length, flags);
    int i;
    for(i=0; i<length; i++)
    {
        fprintf(stderr, "%02X ", payload[i]);
    }

    read_flash_sm(EVT_PACKET_ERROR, 0, 0);
}

    
u8 *bufferpool_request(u8 size)
{
    return (u8*)malloc(size);
}

void bufferpool_release(u8 *p)
{
    free(p);
}

typedef enum {
    TESTRX,
    TESTTX,
    CONSOLE
} testmode_t;

int main(int argc, char **argv)
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
            case 'v':
                voltage_log = 1;
                break;
            case 'r':
                fp = strcmp(optarg, "-") ? fopen(optarg, "r") : stdin;
                if (!fp)
                {
                    fprintf(stderr, "Failed to open %s: %s\n",
                            optarg, strerror(errno));
                    return -1;
                }
                mode = TESTRX;
                break;
                
            case 't':
                fp = strcmp(optarg, "-") ? fopen(optarg, "r") : stdin;
                if (!fp)
                {
                    fprintf(stderr, "Failed to open %s: %s\n",
                            optarg, strerror(errno));
                    return -1;
                }
                mode = TESTTX;
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
    if (mode == CONSOLE)
    {
        open_serial();
        config_port();
        do_console();
    }
    else if (mode == TESTRX)
    {
        const unsigned bufsz = 100;
        char buf[bufsz];
        void * ret;

        while ((ret=fgets(buf, bufsz, fp)) != NULL)
        {
            u8 val = strtoul(buf, NULL, 0);
            
            printf("rx_notify(%02X, 0)\n", val);
            rx_notify(val, 0);
        }
    }
    else if (mode == TESTTX)
    {
        const unsigned bufsz = 100;
        char buf[bufsz];
        void * ret;

        u8 to, code, payload_len;
        u8 got_to=0, got_code=0, got_payload_len=0;
        u8 read_payload = 0;
        u8 payload[32];

        while ((ret=fgets(buf, bufsz, fp)) != NULL)
        {
            u8 val;
            char *s, tag[20], *tagp=tag;
            for(s=buf; *s && *s != ':'; s++)
            {
                *tagp++ = *s;
            }
            *tagp = 0;
            if (*s == ':') 
            {
                ++s;
                while(*s == ' ')
                    ++s;
            }
            else {
                s=buf;
                tag[0] = 0;
            }
                        
            val = strtoul(s, NULL, 0);
            
            if (tag[0]) {                
                printf("tx %s = %X\n", tag, val);
            }

            if (!strcmp(tag, "to")) {
                to = val;
                got_to = 1;
            }
            else if (!strcmp(tag, "code")) {
                code = val;
                got_code = 1;
            }
            else if (!strcmp(tag, "len")) {
                payload_len = val;
                got_payload_len = 1;
            }
            else {
                if (got_to && got_code && got_payload_len) {
                    if (read_payload < payload_len) {                        
                        printf("payload[%d] = %X\n", read_payload, val);
                        payload[read_payload++] = val;
                    }
                    else {
                        printf("payload done.\n");
                        send_msg(to, code, payload_len, payload);
                        return 0;
                    }
                }
                else {
                    printf("All headers not defined yet\n");
                }
            }
                    
                    

        }
    }
}
            
        
        
    
void debug_led(u8 x)
{
}

static inline void addr_to_buf(u32 addr, u8 *buf)
{
    buf[0] = addr>>16;
    buf[1] = addr>>8;
    buf[2] = addr;
}

void buf_to_desc(u8 *buf, logged_data_descriptor_t *desc)
{
    desc->flags = buf[0];
    desc->sequence_number = buf[1];
    desc->data_start_offset = (buf[4]<<16 | buf[3]<<8 | buf[2]);
    desc->data_length = (buf[7]<<16 | buf[6]<<8 | buf[5]);
}

flash_sm_state_t read_flash_sm(cmd_or_event_t event, unsigned length, u8 *data)
{
    static u32 addr;
    static unsigned descriptor_index;
    static flash_sm_state_t state;
    static read_all;
    static cmd_or_event_t last_cmd;
    static unsigned last_length;
    static u8 *last_data;
    int notall = 0;
    
//    fprintf(stderr, "read_flash_sm(%d, %d, ...)\n", event, length);
    
    if (event != EVT_FLASH_PACKET_RECEIVED && event != EVT_PACKET_ERROR)
    {
        last_cmd = event;
        last_length = length;
        last_data = data;
    }
    else if (event == EVT_PACKET_ERROR && (state == STATE_READ_SAMPLES_WAIT || state == STATE_READ_HEADERS_WAIT))
    {
        return read_flash_sm(last_cmd, last_length, last_data);
    }
    
    switch (event)
    {
        case CMD_READ_HEADERS_BEGIN:
            addr = 0;
            /* fall-through */
        case CMD_READ_HEADERS_CONTINUE:
            {                
                u8 buf[4];

                addr_to_buf(addr, buf);                
                buf[3] = sizeof(logged_data_descriptor_t);
            
                state = STATE_READ_HEADERS_WAIT;
                
                send_msg(0xF, 0x53, 4, buf);

                break;
            }
            
        case CMD_READ_SAMPLES_BEGIN:
            descriptor_index = *(unsigned *)data;

            if (descriptor_index > descriptor_count)
            {
                fprintf(stderr, "descriptor index %d is greater than read descriptor count %d\n", descriptor_index, descriptor_count);
                break;
            }
            notall = 1;

            /* fall-through */
        case CMD_READ_SAMPLES_ALL:
            if (!notall)
            {
                read_all = 1;
                descriptor_index = 0;
            }
            
            fprintf(stderr, "Reading %04X - %04X (%04X bytes) described by descriptor %d\n",
                    descriptors[descriptor_index].data_start_offset,
                    descriptors[descriptor_index].data_start_offset + descriptors[descriptor_index].data_length,
                    descriptors[descriptor_index].data_length,
                    descriptor_index);

            if (descriptors[descriptor_index].data_length != 0)
            {
                sample_buffers[descriptor_index] = malloc(descriptors[descriptor_index].data_length);
            }
            else
            {
                fprintf(stderr, "Unknown length: mallocing 512kB\n");
                sample_buffers[descriptor_index] = malloc(512*1024);
            }
            if (!sample_buffers[descriptor_index])
            {
                fprintf(stderr, "malloc error\n");
                break;
            }

            addr = descriptors[descriptor_index].data_start_offset;
            
            /* fall-through */            
        case CMD_READ_SAMPLES_CONTINUE:
            {                
                u8 buf[4];

                addr_to_buf(addr, buf);                
                buf[3] = 15;  /* max */
            
                state = STATE_READ_SAMPLES_WAIT;
                
                send_msg(0xF, 0x53, 4, buf);

                break;
            }
            
        case EVT_FLASH_PACKET_RECEIVED:
            {
                u8 *payload = data;
                switch (state)
                {
                    case STATE_READ_HEADERS_WAIT:
                        {
                            /* descriptor in packet. */
                            logged_data_descriptor_t *desc;
                            
                            if (length != sizeof(logged_data_descriptor_t))
                            {
                                fprintf(stderr, "EVT_FLASH_PACKET_RECIEVED in state READ_HEADERS_WAIT: length is %d, not %d\n",
                                        length, sizeof(logged_data_descriptor_t));
                                return;
                            }


                            desc =  &descriptors[descriptor_count];

                            buf_to_desc(payload, desc);
                            
                            fprintf(stderr, "Descriptor %d @ %X \n"
                                            " flags             %X\n"
                                            " sequence number   %d\n"
                                            " data_start_offset %X\n"
                                            " data_length       %X\n",
                                            descriptor_count, addr, 
                                            desc->flags, 
                                            desc->sequence_number,
                                            desc->data_start_offset,
                                            desc->data_length);
                            
 
                            if (desc->flags != 0xDD)
                            {
                                /* done reading headers */
                                fprintf(stderr, "EVT_FLASH_PACKET_RECIEVED: flags at addr %X are %X, stopping\n", addr, desc->flags);
                                state = STATE_READ_HEADERS_DONE;
                                break;
                            }
                            
                            addr += 8;
                            ++descriptor_count;
                            //fprintf(stderr, "Wait...\r");
                            //usleep(100000);
                            read_flash_sm(CMD_READ_HEADERS_CONTINUE, 0, 0);
                            break;
                        }
                    case STATE_READ_SAMPLES_WAIT:
                        {
                            unsigned buf_offset = addr - descriptors[descriptor_index].data_start_offset;

                            fprintf(stderr, "Read %05X - %05X               \r",
                                    descriptors[descriptor_index].data_start_offset, addr+length);

//                            printf("read_samples_wait: addr %05X length %02d offset %05X \r", addr, length, buf_offset);

                            if (descriptors[descriptor_index].data_length == 0)
                            {
                                /* length not saved.  Keep reading until 8 0xFFs in a row. */
                                int z, y=0;
                                for(z=0; z<length; z++)
                                {
                                    if (payload[z] == 0xFF)
                                    {
                                        ++y;
                                    }
                                }
                                if (y == length)
                                {
                                    fprintf(stderr, "Read 8 FFs -- stopping streaming read (length is %X\n", buf_offset);
                                    descriptors[descriptor_index].data_length = buf_offset;
                                    state = STATE_READ_SAMPLES_DONE;
                                    break;
                                }
                                memcpy(sample_buffers[descriptor_index] + buf_offset, payload, length);
                                addr += length;
                                read_flash_sm(CMD_READ_SAMPLES_CONTINUE, 0, 0);
                            }
                            else
                            {
                                if (buf_offset + length > descriptors[descriptor_index].data_length)
                                {
                                    length = descriptors[descriptor_index].data_length - buf_offset;
                                }

                                memcpy(sample_buffers[descriptor_index] + buf_offset, payload, length);
                                addr += length;

                                if (addr < descriptors[descriptor_index].data_start_offset + descriptors[descriptor_index].data_length)
                                {
                                    read_flash_sm(CMD_READ_SAMPLES_CONTINUE, 0, 0);
                                }
                                else if (read_all)
                                {
                                    ++descriptor_index;
                                    read_flash_sm(CMD_READ_SAMPLES_BEGIN, 4, (u8 *)&descriptor_index);
                                }
                                else
                                {
                                    state = STATE_READ_SAMPLES_DONE;
                                    printf("\nDone; read %d bytes\n", 
                                            descriptors[descriptor_index].data_length);
                                }
                            }
                            break;
                        }
                    default:
                        fprintf(stderr, "Received flash buffer in state %d\n", state);
                        state = STATE_BAD_PACKET;
                }
            }
            break;
        default:
            fprintf(stderr, "read_flash_sm: unknown event %d\n", event);
    }

    return state;
}
                    
            
            
void write_samples_to_file(unsigned index, char *filename, int binary)
{
    FILE *file;
    unsigned si = 0;
    unsigned last_time = 0;

    file = fopen(filename, "w");
    if (!file)
    {
        fprintf(stderr, "Error opening \"%s\" for writing: %s\n", 
                filename, strerror(errno));
        return;
    }
    
    if (!binary)
    {
        for(si=0; si<descriptors[index].data_length; )
        {
            u8 *sp = &sample_buffers[index][si];
            u8 flags = (*sp&0xF);
            u8 len   = (*sp>>4);

            if (flags != 0xA)
            {
                int j, ok=0;
                ++si;
                continue;

                fprintf(stderr, "invalid flags for sample in buffer at offset %X\n", si);
                for(j=0; j<20; j++)
                {
                    if ((*(sp+j)&0xF)==0xA)
                    {
                        si += j;
                        ok=1;
                        break;
                    }
                }
                if(!ok)
                {
                    break;
                }
            }

            {
                unsigned timestamp = sp[5]<<24 | sp[4]<<16 | sp[3]<<8 | sp[2];
                unsigned tdelt = timestamp - last_time;
                int i;
                
                printf("| %02X %02X %6d.%03d.%03d (+%d.%03d.%03d) ",
            //fprintf(stderr, "| %02X %02X %08X %02X%02X\n",
                    sp[0], sp[1],
                    timestamp/1000000, (timestamp/1000)%1000, timestamp%1000,
                    tdelt/1000000, (tdelt/1000)%1000, tdelt%1000);
                for(i=0; i<len; i++)
                {
                    printf("%02X ", sp[6+i]);
                }
                printf("\n");

                if (file)
                {
                    fprintf(file, "| %02X %02X %6d.%03d.%03d (+%d.%03d.%03d) ",
                        sp[0], sp[1],
                        timestamp/1000000, (timestamp/1000)%1000, timestamp%1000,
                        tdelt/1000000, (tdelt/1000)%1000, tdelt%1000);
                    for(i=0; i<len; i++)
                    {
                        fprintf(file, "%02X ", sp[6+i]);
                    }
                    fprintf(file, "\n");
                }

                
            
                last_time = timestamp;

                si += 6+len;
            }
        }
    }
    else
    {
        /* binary */
        size_t ret = fwrite(sample_buffers[index], descriptors[index].data_length, 1, file);
        if (ret != 1)
        {
            fprintf(stderr, "fwrite failed: %s\n", strerror(errno));
        }
    }
    fclose(file);
}
