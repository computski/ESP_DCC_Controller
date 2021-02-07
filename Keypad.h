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

/*in Cpp you can pass a struct by ref though the & symbol*/
//https://stackoverflow.com/questions/15181765/passing-structs-to-functions/15181807
/*calling function just calls with the var, no need for pointers because cpp makes this byRef*/

extern KEYPAD keypad;

//void  keyScan(KEYPAD &keypad);
void keyScan(void);

#endif
