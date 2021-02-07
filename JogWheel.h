// JogWheel.h

#ifndef _JOGWHEEL_h
#define _JOGWHEEL_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

/*handles a rotary encoder with pushbutton.  Uses interrupt on change
the JOGWHEEL structure does not point to a specific LOCO index, rather
each LOCO will have a flag indicating if is controlled by the jog
*/

namespace nsJogWheel {


	struct JOGWHEEL {
		bool    jogEvent;   //rotation or button event for system to process
		bool	jogCW;   //true if rotation was clockwise
		//uint8_t jogTimer;  //used for masking on jogwheel
		bool	jogButtonEvent;
		bool    jogButton; //button is depressed
		bool    jogHeld;  //button held
		uint8_t	jogTick;  //used to countdown 40mS intervals
		uint8_t jogButtonTimer;  //used to detect a button down event and a held event
		uint8_t	pinState;	//used to restore pin state after a button read
		uint8_t	state;
		bool	jogHiSpeed;  //set if user is rotating jog quickly
		bool	jogLoSpeed; //set if user is rotating slowly enough to permit shoot-through on a direction change.
		uint8_t jogSpeedTick;  //used to detect high rotation speed
	};



#define JOG_DEBOUNCE_PERIOD    1  //40ms sec when called every 10mS (mod 4)
#define JOG_LONG_PERIOD		25 //1sec to detect button held (mod 4)
#define JOG_HI_SPEED_PERIOD   4   
#define JOG_LO_SPEED_PERIOD   12

	/*define functions*/
	void jogInit();
	void jogHandler();
	void jogWheelScan();

	//extern JOGWHEEL jogWheel;   //does not work inside the namespace!
}//end of namespace


	/*note, use extern here to define jogWheel but not instantiate.  this allows JogWheel.cpp routines to work with the object
	you then include JogWheel.h into DCCcore.h and instantiate JOGWHEEL jogWheel in DCCcore.cpp (and not its header)*/

	/*looking at the encoder from the front, with the outputs at the top.  leftmost goes to D7 and requires a 12k pulldown, centre
	is 3v3 via 1k pullup and right is D8 which on the nodeMCU has a 12k pulldown.*/



extern nsJogWheel::JOGWHEEL jogWheel;   //need to put the extern here and fully qualify it

#endif

