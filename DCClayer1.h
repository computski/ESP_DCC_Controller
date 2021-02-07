// DCClayer1.h


#ifndef _DCCLAYER1_h
#define _DCCLAYER1_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif



/*longest packet is 6 bytes when using POM and long address*/
	struct DCCBUFFER {
		uint8_t data[6];
		uint8_t packetLen;
		bool  clearToSend;
		bool  longPreamble;
		bool  msTickFlag;
	};

	extern volatile DCCBUFFER DCCpacket;

	void ICACHE_FLASH_ATTR dcc_init(uint32_t pin_info[3], uint8_t invert);

#endif
