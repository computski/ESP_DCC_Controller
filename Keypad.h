// Keypad.h

#ifndef _KEYPAD_h
#define _KEYPAD_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif


//dependencies
#include <Wire.h>


 /*key holds the current valid key. key flag is raised on every press or repeat press if held*/
struct KEYPAD {
	uint8_t group1;
	uint8_t group2;
	uint8_t resA;
	uint8_t resB;
	uint8_t resC;
	uint8_t key;
	char    keyASCII;
	bool    keyFlag;
	bool    keyHeld;
	bool    requestKeyUp;  //callback for a keyup event
	uint8_t keyTimer;
};


extern KEYPAD keypad;

void keyScan(void);

#endif
