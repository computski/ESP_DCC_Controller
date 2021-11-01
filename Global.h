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
* PIN ASSIGNMENTS on the custom PCB implementations, see specific board configurations below
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
*/

#define KEY_ESTOP	26
#define KEY_MODE	25


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
*/


#define BOARD_THREE

 #if defined(BOARD_ONE)
	/*BOARD ONE, blue LCD on 3v3 supply using mjkdz backpack address 0x20
	keyscan uses PCF8574AT on address 0x3F jumpers leftmost. Range 38-3F
	whereas PCF8574T has address range 20-27h
	on board LMD18200T and INA219 fitted*/
#define	PIN_HEARTBEAT	16  //D0
#define	PIN_SCL	5	//D1
#define	PIN_SDA	4	//D2
#define	PIN_ESTOP	0	//D3

#define DCC_PINS \
uint32 dcc_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO14, 14, 0 }; \
uint32 dcc_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO12, 12, 1 }; \
uint32 enable_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 4, 0 }; \
uint32 enable_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 4, 0 };
	//DCC pins are D5 and D6 in antiphase, enable-power pin is D4 (GPIO2) 
	//there is only one enable pin, so just use it on both calls to dccInit

#define	PIN_JOG1	13	//D7
#define	PIN_JOG2	15	//D8

#define KEYPAD_ADDRESS 0x3F   //pcf8574AT
	#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x20, 4, 5, 6, 0, 1, 2, 3, 7, NEGATIVE); //mjkdz backpack

#elif defined(BOARD_TWO)
	/*BOARD TWO, yellow LCD on 5v supply using YwRobot clone backpack address 0x27
	 *keypad uses PCF8574T on address 0x20 jumpers rightmost. Range 20-2F
	 *off-board IBT2 and INA219 fitted*/
#define	PIN_HEARTBEAT	16  //D0
#define	PIN_SCL	5	//D1
#define	PIN_SDA	4	//D2
#define	PIN_ESTOP	0	//D3

#define DCC_PINS \
uint32 dcc_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO14, 14, 0 }; \
uint32 dcc_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO12, 12, 1 }; \
uint32 enable_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 4, 0 }; \
uint32 enable_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 4, 0 };
//DCC pins are D5 and D6 in antiphase, enable-power pin is D4 (GPIO2) 
//there is only one enable pin, so just use it on both calls to dccInit

#define	PIN_JOG1	13	//D7
#define	PIN_JOG2	15	//D8




#define KEYPAD_ADDRESS 0x20   //pcf8574T
	#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); //YwRobot backpack

#elif defined(BOARD_THREE)
	/*BOARD THREE, yellow LCD on 5v supply using YwRobot clone backpack address 0x27
	keyscan uses PCF8574AT on address 0x3F jumpers leftmost. Range 38-3F
	on-board LMD18200T and INA219 fitted*/
#define	PIN_HEARTBEAT	16  //D0
#define	PIN_SCL	5	//D1
#define	PIN_SDA	4	//D2
#define	PIN_ESTOP	0	//D3

#define DCC_PINS \
uint32 dcc_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO12, 12, 0 }; \
uint32 dcc_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO14, 14, 1 }; \
uint32 enable_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 2, 0 }; \
uint32 enable_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 2, 0 };
//DCC pins are D5 (GPIO14) and D6 (GPIO12) in antiphase, enable-power pin is D4 (GPIO2) 
//there is only one enable pin, so just use it on both calls to dccInit
//The LMD18200T only needs D5, however if you use an off board L298 or IBT2 then D6 is also required


#define nDC_PINS \
uint32 pwm_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO14, 14 , 0 }; \
uint32 dir_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 2 , 0 }; 
//DC pins for LMD18200 are pwm on  D4 (GPIO2) and dir on D5 (GPIO14)
//D6 is unused


#define	PIN_JOG1	13	//D7
#define	PIN_JOG2	15	//D8

#define KEYPAD_ADDRESS 0x3F   //pcf8574AT
#define BOOTUP_LCD LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  //YwRobot backpack

#elif defined(BOARD_ESP12_SHIELD)
/*DOIT motor shield for nodeMCU ESP12. Need to re-assign the pin functions and PIN_ESTOP is not available. Note
that PIN_POWER_ALT is defined because this board uses an L293, and uses D1-4 for control, which requires us to
move the I2C bus to D5 and D6 */

#define	PIN_HEARTBEAT 16  //D0

#define DCC_PINS \
uint32 dcc_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO0, 0 , 0 }; \
uint32 enable_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO5, 5 , 0 }; \
uint32 dcc_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 2 , 0 }; \
uint32 enable_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO4, 4 , 0 };
//DCC pins are D3,4 (dir on module GPIO0,2) in phase.  power enable pins are D1,D2 (pwm on module GPIO5,4) active hi
//Each channel in the L293D can support max 600mA so keep two in phase


#define nDC_PINS \
uint32 pwm_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO5, 5 , 0 }; \
uint32 pwm_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO4, 4, 0 };\
uint32 dir_info[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO0, 0 , 0 }; \
uint32 dir_infoA[4] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO2, 2 , 0 }; 
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
so not clear why the pwm runs at full power on boot.  gpio 5,4 should be low. code issue?
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


#endif


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

