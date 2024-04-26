#pragma once
//https://arduino-esp8266.readthedocs.io/en/latest/reference.html?highlight=analogwrite#analog-output
/*
Can use the arduino library functions for this
analogWrite(pin, value)
value is in the range 0-255 by default
analogWriteResolution(bits) can be used.  DCC uses 127 speed steps, so we can go with 0-255 and just double up
in any even we will need to do scaling because we might see 28 step packets or 127 step packets.

Ah but here's the crunch.  we cannot set this to write to both ports simultaneously.  there will be too much 
jitter using the arduino routine so i have no choice but to write my own.

The DCC timebase is 58uS edges which is 8.6khz for a full cycle.  a bit low because decoders typically
put 15khz out.

lets run at 14khz, which is 357 counts at 200nS ticks (which is what we have from the time div16 arrangement)
but that implies the actual clock is 80Mhz.

at the lowest PWM setting, on 127 steps, its 1% duty (less), which would be 4 ticks and arugably that's a problem as its
at best 64 instructions been ISR calls.  28 steps is 3%, or 11 ticks.


The DCC routine reads data into a double buffer and immediately sets a CTS flag allow main prog to overwrite that buffer
at its convenience.  The DCC routine would clock through preabmle and a minimum of 3 bytes before it reads the buffer again.
It just assumes the buffer will hold valid data at this point.

The DC routine needs to emulate this; copy the data (but this might take too long if we are servicing a short int)
before calculating and setting up the right direction and duty cycle.

and the routine also sets a 10mS tick flag.  thats 140 duty cycles elapsed.

To speed up processing we could calculate the duty cycle in the main loop just after populating the buffer, and then the 
DCpwm routine would simply read that duty cycle/dir value.  Or it does the calc during the low part of duty cycle because
its highly likely we will keep to duties below 80% tops.

the routine will only read dir/speed packets for loco 3.  all others are ignored.




*/
