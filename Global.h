// Global.h

#ifndef _GLOBAL_h
#define _GLOBAL_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif

//see DDCcore.h for IP address and websocket port

/*Set Max loco and turnouts here. If you increase max loco or turnout, beware of exceeding the EEPROM dimensions*/
/*and more importantly beware exceeding the JSON output buffer length, 800 is ok for 8 locos*/
/*important: if you change max loco, change the software version date in DCCcore.h to force a wipe and reload of
the EEPROM*/
#define	MAX_LOCO	8   
#define	MAX_TURNOUT	8
#define LOCO_ESTOP_TIMEOUT 8
//define key-codes for these virtual keys on the keypad
#define KEY_ESTOP	26
#define KEY_MODE	25


/*
* PIN ASSIGNMENTS for the NodeMCU module (D0-D8 here is specific to the nodeMCU) as used on some of the custom PCB implementations, 
* see specific board configurations below
* GPIO16   D0  drives the on-motherboard LED, active low. Used as heartbeat indicator and jogwheel-pushbutton active hi.
* GPIO5    D1  default SCL on arduino wire.remember the pullup
* GPIO4    D2  default SCA on arduino wire.remember the pullup.The keypad module has the pullup
* GPIO0    D3  has 12k pullup, wired via pull down res for Flash(boot only).we can use for local estop by pulling low also via JP2.
* GPIO2    D4  has 12k pullup, used as Power enable (also drives the blue LED on the ESP8266 module)
* GPIO14   D5  Dcc signal antiphase (used to drive charge pump)
* GPIO12   D6  Dcc signal
* GPIO13	D7 used for jogwheel
* GPIO15	D8 has 12k pulldown used for jogwheel
* GPIO3	RX via 470 into the USB chip
* GPIO1	TX via 470 into the USB chip
*
* A minimal hardware option with no LCD display, no keypad and no jogwheel would require you to connect a pulldown button
* to GPIO0/D3 to act as Emergency Stop, and LED from GPIO2/D4 to ground via a res to indicate whether power is on/off.
* optionally can also add another LED from GPIO16/D0 to ground via a res to indicate heartbeat status.  Or you could light-pipe
* these two existing on-board LEDs through your case.
*
* the MODE button is only implemented via I2C keypad scan.  only makes sense to implement MODE if a display and keypad are present.
*
* Note: hardware bug, sometimes the jogwheel will be 'stuck' between detents.  It might pull D8 high as a result which will prevent
* the unit from booting.  Rotate the jogwheel and the unit should boot.
*/



/*HARDARE CONFIGURATION
Below are some predefined boards.  These define the nodeMCU functions pin by pin, as well as the LCD backpack
the current sensor address, the keyscanner address.

The DCC pins are each defined through an array[4] of uint32.  First element is 
 PERIPHS_IO_MUX_MTDI_U, second is the GPIO reference, third is the GPIO pin as an integer and 4th is
zero for non inverted and 1 for inverse phase, i.e. you can have say two outputs in phase or antiphase
The backslash notation tells the compiler this is a multiline macro

If you add #define INA_ADDRESS x below, your address x will be picked up by DCCcore.cpp when it boots. Default is 0x40

To create a DC system, ensure you define DC_PINS macro in the board of your choice.  DC_PINS is optional. If you 
comment-out DC_PINS or omit it entirely the compiler will expect to find DCC_PINS and build a DCC system.
If both are missing the compiler will throw an error.
Easiest way to disable DC_PINS entry is rename it nDC_PINS

For jogwheel, if you are using a HW40 device add this line to your hardware configuration
otherwise, omit this line or change it to #define nROTARY_HW40
#define ROTARY_HW40
*/


#define nNODEMCU_CUSTOM_PCB_3
#define nNODEMCU_DOIT_SHIELD
#define WEMOS_D1R1_AND_L298_SHIELD

 #if defined(NODEMCU_OPTION1)
	/*BOARD ONE, blue LCD on 3v3 supply using mjkdz backpack address 0x20
	keyscan uses PCF8574AT on address 0x3F jumpers leftmost. Range 38-3F
	whereas PCF8574T has address range 20-27h on board LMD18200T and INA219 fitted
	Uses custom PCB and a nodeMCU*/
#define	PIN_HEARTBEAT	16  //D0
#define	PIN_SCL	5	//D1
#define	PIN_SDA	4	//D2
#define	PIN_ESTOP	0	//D3

#define DCC_PINS \
dcc_init(12, 2, true, false);\
dcc_init(14, 2, false, false);
	//DCC pins are D5 and D6 in antiphase, enable-power pin is D4 (GPIO2) 
	//there is only one enable pin, so just use it on both calls to dccInit

#define	PIN_JOG1	13	//D7
#define	PIN_JOG2	15	//D8

#define KEYPAD_ADDRESS 0x3F   //pcf8574AT
	#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x20, 4, 5, 6, 0, 1, 2, 3, 7, NEGATIVE); //mjkdz backpack

#elif defined(NODEMCU_OPTION2)
	/*BOARD TWO, yellow LCD on 5v supply using YwRobot clone backpack address 0x27
	 *keypad uses PCF8574T on address 0x20 jumpers rightmost. Range 20-2F
	 *off-board IBT2 and INA219 fitted. Uses custom PCB and nodeMCU*/
#define	PIN_HEARTBEAT	16  //D0
#define	PIN_SCL	5	//D1
#define	PIN_SDA	4	//D2
#define	PIN_ESTOP	0	//D3

#define DCC_PINS \
dcc_init(12, 2, true, false);\
dcc_init(14, 2, false, false);
//DCC pins are D5 and D6 in antiphase, enable-power pin is D4 (GPIO2) 
//there is only one enable pin, so just use it on both calls to dccInit

#define	PIN_JOG1	13	//D7
#define	PIN_JOG2	15	//D8




#define KEYPAD_ADDRESS 0x20   //pcf8574T
	#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); //YwRobot backpack

#elif defined(NODEMCU_CUSTOM_PCB_3)
	/*BOARD THREE, yellow LCD on 5v supply using YwRobot clone backpack address 0x27
	keyscan uses PCF8574AT on address 0x3F jumpers leftmost. Range 38-3F
	on-board LMD18200T and INA219 fitted, custom PCB used with nodeMCU fitted.*/
#define	PIN_HEARTBEAT	16  //D0
#define	PIN_SCL	5	//D1
#define	PIN_SDA	4	//D2
#define	PIN_ESTOP	0	//D3

#define DCC_PINS \
dcc_init(12,2,true,false);\
dcc_init(14,2,false,false);

//DCC pins are D5 (GPIO14) and D6 (GPIO12) in antiphase, enable-power pin is D4 (GPIO2) 
//there is only one enable pin, so just use it on both calls to dccInit
//The LMD18200T only needs D5, however if you use an off board L298 or IBT2 then D6 is also required


#define nDC_PINS \
dc_init(14, 2, true, false);\

//DC pins for LMD18200 are pwm on  D4 (GPIO2) and dir on D5 (GPIO14)
//D6 is unused


#define	PIN_JOG1	13	//D7
#define	PIN_JOG2	15	//D8

#define KEYPAD_ADDRESS 0x3F   //pcf8574AT
#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  //YwRobot backpack

#elif defined(NODEMCU_DOIT_SHIELD)
/*DOIT motor shield for nodeMCU ESP12. Need to re-assign the pin functions and PIN_ESTOP is not available. Note
that PIN_POWER_ALT is defined because this board uses an L293, and uses D1-4 for control, which requires us to
move the I2C bus to D5 and D6.  The DOIT sheild needs and external INA219 current monitor and it has no on-board
regulator or current sense resistor.*/

#define	PIN_HEARTBEAT 16  //D0

#define nDCC_PINS \
dcc_init(0, 5, true, false);\
dcc_init(2, 4, true, false);
//DCC pins are D3,4 (dir on module GPIO0,2) in phase.  power enable pins are D1,D2 (pwm on module GPIO5,4) active hi
//Each channel in the L293D can support max 600mA so keep two in phase


#define DC_PINS \
dc_init(5, 0, true, false);\
dc_init(4, 2, true, false);

//DC pwm pins D1,D2 are in phase.  dir pins D3,D4 are also in phase 


/*
GPIO16: pin is high at BOOT.
GPIO0: boot failure if pulled LOW.
GPIO2: pin is high on BOOT, boot failure if pulled LOW.
GPIO15: boot failure if pulled HIGH.
GPIO3: pin is high at BOOT.
GPIO1: pin is high at BOOT, boot failure if pulled LOW.
GPIO10: pin is high at BOOT.
GPIO9: pin is high at BOOT.
*/


#define	PIN_SCL 14		//D5
#define	PIN_SDA	12		//D6
#define	PIN_JOG1	13	//D7
#define	PIN_JOG2	15	//D8
	/*Yellow LCD on 5v supply using YwRobot clone backpack address 0x27
	keyscan uses PCF8574AT on address 0x3F jumpers leftmost. Range 38-3F
	on-board LMD18200T and INA219 fitted*/
#define KEYPAD_ADDRESS 0x3F   //pcf8574AT
#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  //YwRobot backpack


#elif defined(WEMOS_D1R1_AND_L298_SHIELD)
/*Wemos D1-R1 stacked with L298 sheild, note that the D1-R2 is a newer model with different pinouts*/
/*Cut the BRAKE jumpers on the  L298 shield. These are not required and we don't want them driven by
the I2C pins as it will corrupt the DCC signal.
The board has an Arduino form factor, the pins are as follows
D0 GPIO3   RX
D1 GPIO1   TX
D2 GPIO16  Jog pushbutton.   IO16 cannot support an interrupt.
D3 GPIO5   DCC enable (pwm)
D4 GPIO4   Jog1
D5 GPIO14  DCC signal (dir)
D6 GPIO12  DCC signal (dir)
D7 GPIO13  DCC enable (pwm)
D8 GPIO0   SDA, needs off board 12k pullup
D9 GPIO2   SCL, needs off board 12k pullup.  IO2 also drives the ESP-12 module led.
D10	GPIO15 Jog2. WeMos has 10k to ground.  cannot use this for I2C (as we need pull ups).  boot will fail if this is held high at boot.
Note: D3,5,6,7 are dictated by the L298 module


IO16  no ints, high on boot, no I2C
D8 IO0 boot fails if low.
D9 IO2 boot fails if low
IO15  boot fails if high.  cannot be used for I2C.  cannot be used for jog1/2
IO4 ok
//important:  these I2C and Jogwheel pin assignments are the best arrangement, however we need to delay feeding 3.3v to the 
//jogwheel itself during the boot phase.  to do this, place 1k in series with 3v3 into a 47uF cap to ground.  the junction of the 
//two feeds the "gnd" pin on the HW40 board.  You also need a diode from this junction with its cathode (-side) to the RESET line.
//now during power-up or a reset, the RC junction will stay low long enough that the Io15 line is low during boot.

The jogWheel-on-a-board is wired like this:
Gnd links to the mid-point of the jogwheel, it also acts as one terminal of the switch
SW is the second push switch terminal
CK and DA are the jogwheel outputs
+ is fed via 10 resistors to each of CK and DA.
//https://lastminuteengineers.com/rotary-encoder-arduino-tutorial/

For our application here, we need to reverse the polarity on it.  i.e. "gnd" is connected to 3v3 and so SW will be active hi
CK and DA will be pulled low via "+" which is to be connected to system ground.

If you use a naked jogwheel, you need to arrange resistor pulldowns on all its outputs, and the outputs need to be active hi.
This also allows you to utilise an external heartbeat LED.
*/

#define USE_ANALOG_MEASUREMENT
//#define ANALOG_SCALING 1.95  //1.65v is 512 conversion for 1000mA (1.18 to match multimeter RMS)
#define ANALOG_SCALING 3.9  //when using A and B in parallel  (2.36 to match multimeter RMS)

#define	PIN_SCL		2 
#define	PIN_SDA		0 

//#define	PIN_HEARTBEAT 16  //not used, instead for the WeMos board we will define PIN_JOG_PUSH
#define ROTARY_HW40 //in this case we are using a rotary HW40 device, if not, then change to nROTARY_HW40
#define PIN_JOG_PUSH	16   
#define	PIN_JOG1	4  
#define	PIN_JOG2	15 //IO15 has on board 10k pulldown, IO15 must be low for boot

#define DCC_PINS \
dcc_init(12,5,true,false);\
dcc_init(14,13,true,false);


#define KEYPAD_ADDRESS 0x21   //pcf8574
//addr, en,rw,rs,d4,d5,d6,d7,backlight, polarity.   we are using this as a 4 bit device
//my display pinout is rs,rw,e,d0-d7.  only d<4-7> are used. <210>  appears because bits <012> are mapped
//as en,rw,rs and we need to reorder them per actual order on the hardware, 3 is mapped to the backlight
//<4-7> appear in that order on the backpack and on the display.
#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  //YwRobot backpack

#endif




//set nTRACE to disable, TRACE to enable serial tracing.  Disable for production.
#define nTRACE   

#ifndef TRACE
	#define trace(traceCodeBlock) ;
#else
	#define trace(traceCodeBlock) traceCodeBlock
#endif

/*If TRACE is defined, we include the traceCodeBlock statements
otherwise we write ; and effectively exclude what's inside trace()
This is GLOBAL.  Defining TRACE within a module has no effect
you'd need to move this macro from global.h to each file*/


/*end of Global.h*/
#endif

