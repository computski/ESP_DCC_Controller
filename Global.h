// Global.h

#ifndef _GLOBAL_h
#define _GLOBAL_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

//see DDCcore.h for IP address and websocket port


/*
* PIN ASSIGNMENTS
* GPIO16   D0  drives the on-motherboard led, active low. Used as heartbeat indicator and jogwheel-pushbutton active hi.
* GPIO5    D1  default SCL on arduino wire.remember the pullup
* GPIO4    D2  default SCA on arduino wire.remember the pullup.The keypad module has the pullup
* GPIO0    D3  has 12k pullup, wired via pull down res for Flash(boot only).we can use for local estop by pulling low also via JP2.
* GPIO2    D4  has 12k pullup, used as Power enable (also drives the blue LED on the ESP sub module)
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
*/

#define	PIN_HEARTBEAT	16
#define	PIN_SCL	5
#define	PIN_SDA	4
#define	PIN_ESTOP	0
#define	PIN_POWER	2
#define	PIN_DCC		14
#define	PIN_DCC_ALT	12
#define	PIN_JOG1	13
#define	PIN_JOG2	15
#define PIN_RX	3

#define KEY_ESTOP	26
#define KEY_MODE	25




/*2020-03-29 note calling LiquidCrystal_I2C will create a reference to LiquidCrystal.h*/


/*hardware configuration*/
#define BOARD_THREE
 

 #if defined(BOARD_ONE)
	/*BOARD ONE, blue LCD on 3v3 supply using mjkdz backpack address 0x20
	keyscan uses PCF8574AT on address 0x3F jumpers leftmost. Range 38-3F
	whereas PCF8574T has address range 20-27h
	on board LMD18200T and INA219 fitted*/
	#define KEYPAD_ADDRESS 0x3F   //pcf8574AT
	#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x20, 4, 5, 6, 0, 1, 2, 3, 7, NEGATIVE); //mjkdz backpack
	#define POWER_ON  HIGH
	#define POWER_OFF  LOW

#elif defined(BOARD_TWO)
	/*BOARD TWO, yellow LCD on 5v supply using YwRobot clone backpack address 0x27
	 *keypad uses PCF8574T on address 0x20 jumpers rightmost. Range 20-2F
	 *off-board IBT2 and INA219 fitted*/
	#define KEYPAD_ADDRESS 0x20   //pcf8574T
	#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); //YwRobot backpack
#define POWER_ON  HIGH
#define POWER_OFF  LOW

#elif defined(BOARD_THREE)
	/*BOARD THREE, yellow LCD on 5v supply using YwRobot clone backpack address 0x27
	keyscan uses PCF8574AT on address 0x3F jumpers leftmost. Range 38-3F
	on-board LMD18200T and INA219 fitted*/
#define KEYPAD_ADDRESS 0x3F   //pcf8574AT
#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  //YwRobot backpack
#define POWER_ON  HIGH
#define POWER_OFF  LOW

#endif

/*its not necessary to define the bridge hardware because they all have positive EN
however, we may need to define for scaling resistor purposes*/

/*if you increase max loco or turnout, beware of exceeding the EEPROM dimensions*/
/*and more importantly beware exceeding the JSON output buffer length, 800 is ok for 8 locos*/
/*important: if you change max loco, change the software version date in DCCcore.h to force a wipe and reload of
the EEPROM*/
#define	MAX_LOCO	8   
#define	MAX_TURNOUT	8
#define LOCO_ESTOP_TIMEOUT 8

//set nTRACE to disable, TRACE to enable serial tracing.  Disable for production.
#define nTRACE   

//https ://stackoverflow.com/questions/7246512/ifdef-inside-a-macro

#ifndef TRACE
	#define trace(traceCodeBlock) ;
#else
	#define trace(traceCodeBlock) traceCodeBlock
#endif

//If TRACE is defined, we include the traceCodeBlock statements
//otherwise we write ; and effectively exclude what's inside trace()
//BUT this is global.  defining TRACE within a module has no effect
//you'd need to move this macro from global.h to each file

/*end of Global.h*/
#endif

