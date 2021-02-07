
#include "Global.h"
#include "JogWheel.h"
#include <arduino.h>

using namespace nsJogWheel;
/*JogWheel 2020-05-19
handles a rotary encoder, cheap mechanical type

Some NodeMCU inputs have pull down or pull up resistors on them, most pins also have internal WPU.  
D0 (GPIO16) has a WPD instead. to preserve output state we need to access the low level registers

you cannot read the output latch unless the pin was previously declared as input_output.  Arduino
does not directly support this. easier to add pinState to the JOGWHEEL struct.  D0 here is used to monitor
pushbutton state.

Rotary encoder is a 2 output device moving from Step1 to Step2 and issues a quadrature code as it rotates.
These devices have lots of contact bounce.  The scheme is based around a state engine which is driven by the pin states
as sampled on change.  both pins have interrupt on change assigned to them.  The alternative is to constantly poll
the pins but this needs to be 5mS intervals or less.


 *   Position   Bit1   Bit2
 *   ----------------------
 *     Step1     0      0
 *      1/4      1      0
 *      1/2      1      1
 *      3/4      0      1
 *     Step2     0      0

 Gray code method.  Credit to site below
 http://www.buxtronix.net/2011/10/rotary-encoders-done-properly.html

 we attach an interrupt on CHANGE to both pins.  This method works reliably at any rotor speed and without the need
 to add capacitors as low pass filters.

 Hardware note: PIN_JOG2 already has a 12k pulldown on the NodeMCU board.  PIN_JOG1 has an external 12k pulldown added, and
 the pullup resistor is 1k.  The pushbutton is active high through a 1k pullup.

 BUG: if you change rotor direction, the first detent generates no output, second detent will generate an output in the new direction

2020-05-27 on 128 step mode, a full 6 rotations are required to traverse the range because there are 20 detents.  jog code now
measures time between detents and if this is short, sets jogHiSpeed flag which indicates the user is turning the knob
quickly.  main code acts on this by incr/dec in steps of 5 rather than 1.  This gives course/fine control.

2021-01-26 jogLoSpeed flag indicates if user is rotating slowly - approx two detents per sec, this is used to control
shoot through after a direction change in shunter mode
*/

#define R_START 0x0
#define R_CW_FINAL 0x1
#define R_CW_BEGIN 0x2
#define R_CW_NEXT 0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT 0x6
/*Values returned by the state engine*/
#define DIR_NONE 0x0  //not a complete step yet
#define DIR_CW 0x10  //clockwise step
#define DIR_CCW 0x20  //counter-clockwise step

const uint8_t ttable[7][4] = {
	// R_START
	{R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
	// R_CW_FINAL
	{R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
	// R_CW_BEGIN
	{R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
	// R_CW_NEXT
	{R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
	// R_CCW_BEGIN
	{R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
	// R_CCW_FINAL
	{R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
	// R_CCW_NEXT
	{R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};



void nsJogWheel::jogInit(){
pinMode(PIN_JOG1, INPUT); 
pinMode(PIN_JOG2, INPUT);   //has a 12k pulldown on it already
attachInterrupt(digitalPinToInterrupt(PIN_JOG1), jogHandler,CHANGE);
attachInterrupt(digitalPinToInterrupt(PIN_JOG2), jogHandler,CHANGE);
jogWheel.state== R_START;
}


void nsJogWheel::jogHandler() {
	/*sample input pins*/
	uint8_t pinstate = (digitalRead(PIN_JOG1) << 1) | digitalRead(PIN_JOG2);
	/*determine new state from the pins and state table*/
	jogWheel.state = ttable[jogWheel.state & 0xf][pinstate];

	/*do we have a result?*/
	switch (jogWheel.state & 0x30) {
	case DIR_CW:
		jogWheel.jogCW = true;
		jogWheel.jogEvent = true;
		jogWheel.jogHiSpeed = (jogWheel.jogSpeedTick <= JOG_HI_SPEED_PERIOD) ? true : false;
		jogWheel.jogLoSpeed = (jogWheel.jogSpeedTick >= JOG_LO_SPEED_PERIOD) ? true : false;
		jogWheel.jogSpeedTick = 0;
		break;

	case DIR_CCW:
		jogWheel.jogCW = false;
		jogWheel.jogEvent = true;
		jogWheel.jogHiSpeed = (jogWheel.jogSpeedTick <= JOG_HI_SPEED_PERIOD) ? true : false;
		jogWheel.jogLoSpeed = (jogWheel.jogSpeedTick >= JOG_LO_SPEED_PERIOD) ? true : false;
		jogWheel.jogSpeedTick = 0;
		break;
	}
	/*consuming routine needs to clear jogEvent flag*/
}


/*call at 10mS intervals to clear the jog mask and debounce the button
Note, the main code drives jogWheel.pinState and this is asserted here every
10mS.  Don't want to drive it in both DCCcore and here*/
void nsJogWheel::jogWheelScan() {

	/*The heartbeat pin on the Node MCU is GPIO16, it has a resistor/LED chain to 3v3 and is active low.
	It cannot have WPU enabled, but does have WPD.  The LED has some capacitance, so we need to drive the
	pin active low first before reading*/
	
	jogWheel.jogTick++;
	if (jogWheel.jogTick % 4 == 0) {
		/*read button every 40mS. First capture existing output state*/

		/*drive low and enable WPD*/
		digitalWrite(PIN_HEARTBEAT, LOW);
		pinMode(PIN_HEARTBEAT, INPUT_PULLDOWN_16);

		/*read it, all jog rotation and button pushes are active high*/
		if (digitalRead(PIN_HEARTBEAT) == HIGH) {
			jogWheel.jogButtonTimer += jogWheel.jogButtonTimer != 255 ? 1 : 0;
		}
		else {
			jogWheel.jogButtonTimer = 0;
			jogWheel.jogButton = false;
			jogWheel.jogHeld = false;
		}

		pinMode(PIN_HEARTBEAT, OUTPUT);
		/*declare an event*/
		if (jogWheel.jogButtonTimer == JOG_DEBOUNCE_PERIOD) {
			jogWheel.jogButton = true;
			jogWheel.jogButtonEvent = true;
		}
		if (jogWheel.jogButtonTimer == JOG_LONG_PERIOD) {
			jogWheel.jogHeld = true;
			jogWheel.jogButtonEvent = true;
		}
	}
	
	jogWheel.jogSpeedTick += jogWheel.jogSpeedTick == 255 ? 0 : 1;
	
	/*restore state on exit*/
	digitalWrite(PIN_HEARTBEAT, jogWheel.pinState);

}


