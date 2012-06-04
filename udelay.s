/*
> What is the calling convention in C (avr-gcc)?

register usage

Parameters* Parameter 1 Parameter 2 Parameter 3 Parameter 4

f(p8,p8) R25 R24
f(p8,p8,p8,p8) R25 R24 R23 R22
f(p16,p16) R25:R24? R23:R22
f(p16,p16,p16) R25:R24 R23:R22 R21:R20
f(p32,p32) R25:R24:R23:R22? R21:R20:R19:R18
f(p32,p32,p32) R25:R24:R23:R22 R21:R20:R19:R18 R17:R16:R15:R14
f(p8,p16,p32) R25 R24:R23 R22:R21:R20:R19

return value
p8 R25:R24??
p16 R25:R24
p32 R25:R24:R23:R22

? MSB left, LSB right (r25 - lower, r24 - higher, and so on)
?? R25 is cleared

Call-used registers: r18-r27, r30-r31
Call-saved registers: r2-r17, r28-r29
Fixed registers: r0, r1

r0 can be trashed anywhere but in ISR.
r1 is assumed to be always 0.  "clr r1" before calling C code.

*/

#define PIND 0x10
#define DDRD 0x11
#define PORTD 0x12
#define PINC 0x13
#define DDRC 0x14
#define PORTC 0x15

#define W1_PORT PORTC
#define W1_PIN  0
#define W1_DIR  DDRC
#define W1_IN   PINC



/* Run 4*r25 nops  (assume 4Mhz clock) 
 * note: rcall = 3cyc, ret = 4cyc 
 */

/* void udelay(unsigned short us) */
.global udelay
udelay:
1:
    subi r24, 1             ; decrement LSB
    brlo 2f                 ; branch if 0
    rjmp 1b                 ; 1+1+2 cycles @ 4Mhz = 1us
2:
    ; this is going to add unaccounted cycles every 4*256 cycles
    cpi r25, 0              ; check MSB
    breq 3f                 ; zero - done
    subi r25, 1             ; decrement msb
    rjmp 1b    
3:    
    wdr                     ; reset the watchdog
    ret
    
/* void mdelay(unsigned short ms) */
.global mdelay
mdelay:
    mov r18, r24
    mov r19, r25
    ldi r20, 0xFF
    
  3:
    cp r18, r1
    brne 1f  
    cp r19, r1
    brne 1f
    ret
  1:
    ldi r24, 0xe8
    ldi r25, 3
    rcall udelay
    subi r18, 1 ; decrement LSB
    cp r18, r20
    brne 3b
    subi r19, 1 ; decrement MSB
    rjmp 3b
    
