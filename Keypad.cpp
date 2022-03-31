// 
// 
// 
#include "Global.h"
#include "Keypad.h"

/* KEYPAD scan routine using a PCF8574 GPIO expander and a 4x4 key matrix.
 * This expander is well suited to the task as it is simple to use and has
 * weak pullups.  We strobe it by pulling each row low in turn and reading
 * the columns.  Boards are available that all the I2C bus to be daisy chained through
 *
 * Hardware notes: keypad is 4 cols and 4 rows looking at the keyside of the connector
 * cols are <7-4> left to right, rows are <3-0> with 3 at top and 0 at bottom
 * the key returned is 1 for the bottom right key and 16 for the top left
 * additionally there are codes generated for pressing pairs of keys in cols 7-6 and 5-4
 *
 * The main prog loop must consume a new keyFlag event within a 90mS debounce period else
 * the keyScan code will clear both the key and keyFlag
 *
 * You can pull whole-columns to zero via an external switch and this will encode as 25 thro 28
 * this is how we process eStop and Mode
 * Pull P7 low to trigger MODE, pull P6 low to trigger ESTOP 
 * *
 *Global.h contains the KEYPAD_ADDRESS
 */



#include <Arduino.h>

//key holds the current valid key. key flag is raised on every press or repeat press if held

#define KEY_REPEAT_PERIOD1    50  //.5 sec when called every 10mS
#define KEY_REPEAT_PERIOD2    20  //.2 sec when called every 10mS
#define KEY_REPEAT_LONG		  150 //1.5 sec long repeat for MODE	

const char ASCIImapping[] = "D#0*C987B654A321";

/*call keyScan every 10mS, we strobe the rows <3-0> and read the cols
 *we need 3 consistent reads before we declare a result, giving a response time of
 *approx 30mS
 *group1 contains rows 1,0 and group 2 contains rows 3,2 as the most and least significant nibbles of each
 *group result.
 */

void keyScan(void)
{
	uint8_t keyPress;
	Wire.beginTransmission(KEYPAD_ADDRESS);
	Wire.write(0b11111101); //row 1
	Wire.endTransmission();
	//to avoid compiler candidate errors, ensure you cast to uint8_t and 1u to get the right method
	Wire.requestFrom((uint8_t)KEYPAD_ADDRESS, 1u, true);
	keypad.group1 = Wire.read();
	keypad.group1 &= 0b11110000;
	Wire.beginTransmission(KEYPAD_ADDRESS);
	Wire.write(0b11111110);  // row 0
	Wire.endTransmission();
	Wire.requestFrom((uint8_t)KEYPAD_ADDRESS, 1u, true);
	keypad.group1 |= (Wire.read() >> 4);

	Wire.beginTransmission(KEYPAD_ADDRESS);
	Wire.write(0b11110111); //row 3
	Wire.endTransmission();
	Wire.requestFrom((uint8_t)KEYPAD_ADDRESS, 1u, true);
	keypad.group2 = Wire.read();
	keypad.group2 &= 0b11110000;
	Wire.beginTransmission(KEYPAD_ADDRESS);
	Wire.write(0b11111011); //row 2
	Wire.endTransmission();
	Wire.requestFrom((uint8_t)KEYPAD_ADDRESS, 1u, true);
	keypad.group2 |= (Wire.read() >> 4);

	/*now resolve the captured group codes
	 *group1 msb are cols from r1, lsb cols from r0
	 *group2 msg as cols from r3, lsb cols from r2
	 *key positions are numbered 1 bottom right, through 16 top left
	 *zero represents no key
	 *double-key presses are mapped as 17 for r0 c<0,1> then 18 for r0 c<4,3>
	 *with top left two keys generating 24
	 *codes 25 through 28 are triggered by a pulldown switch on the column.
	 *use of do{}while(0) is a neat way to bracket a block and then use break to exit on a result/

	/*priority is eStop and mode keys*/
	do {
		/*2019-11-16 we strobe the rows.  instead process the columns*/
		uint8_t c= keypad.group1 | keypad.group2;
		if ((c & 0b10001000) == 0) { keyPress = 25;break; }  //Mode
		if ((c & 0b01000100) == 0) { keyPress = 26;break; }  //eStop
		if ((c & 0b00100010) == 0) { keyPress = 27;break; }
		if ((c & 0b00010001) == 0) { keyPress = 28;break; }

		/* next priority are double-key pairs where
		 * row 4 leftmost 2 buttons returns 24, row 0 rightmost buttons return 17
		 * then single key matches, with 16 being top left button, 1 bottom right
		 */

		uint16_t k = keypad.group2 << 8;
		k += keypad.group1;

		for (keyPress = 24;keyPress > 0;keyPress--)
		{
			if (keyPress > 16)
			{
				if (((0b11 << ((keyPress - 17) * 2)) & k) == 0) { break; }
			}
			else
			{
				if (((k >> (keyPress - 1)) & 0x01) == 0) { break; }
			}
		}
		/*if no key, keyPress will be zero because the value is decremented but contents of loop are not executed*/
	} while (false);

	keypad.resC = keypad.resB;
	keypad.resB = keypad.resA;
	keypad.resA = keyPress;
	keypad.keyTimer -= keypad.keyTimer == 0 ? 0 : 1;


	if ((keypad.resA == keypad.resB) && (keypad.resB == keypad.resC))
	{
		//debounce ok. delcare a result
		if (keyPress == 0)
		{
			/*no key is pressed*/
			keypad.keyFlag = false;
			keypad.keyHeld = false;
			keypad.key = 0;
			/*if a keyUp was requested, set flag, but we don't want to keep setting it so clear the request*/
			if (keypad.requestKeyUp) { 
				keypad.requestKeyUp = false;
				keypad.keyFlag = true;
			}
		}
		else
		{
			if (keypad.key == keyPress)
			{
				/*key held but only declare after timeout clears*/
				
				if (keypad.keyTimer == 0)
				{
					keypad.keyHeld = true;
					keypad.key = keyPress;
					keypad.keyTimer = KEY_REPEAT_PERIOD2;
					keypad.keyFlag = true;
				}		
			}
			else
			{
				/*new key pressed*/
				keypad.keyHeld = false;
				keypad.keyFlag = true;
				keypad.key = keyPress;
				/*2019-11-18 special case for MODE key, has a long initial repeat period*/
				if (keypad.key == 25) {
					keypad.keyTimer = KEY_REPEAT_LONG;
				}
				else
				{
					keypad.keyTimer = KEY_REPEAT_PERIOD1;
				}
			}

		}
		/*lookup ASCII value*/
		if ((keypad.key >= 1) && (keypad.key <= 16))
		{
			keypad.keyASCII = ASCIImapping[keypad.key - 1];
		}
		else
		{
			keypad.keyASCII = 0;
		}

	}  //end debounce

} //end function


