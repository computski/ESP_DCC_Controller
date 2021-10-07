// WiThrottle.h
//Implements the JRMI WiThrottle protocol over a TCP socket
//https://www.jmri.org/help/en/package/jmri/jmrit/withrottle/Protocol.shtml

#ifndef _WITHROTTLE_h
#define _WITHROTTLE_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include <ESPAsyncTCP.h>  //Github me-no-dev/ESPAsyncTCP
#include <string>   //required if you wish to compile in arduino IDE this is the std::string library

namespace nsWiThrottle {

#define WITHROTTLE_TIMEOUT	3  //3 sec timeout

	struct CLIENT_T {
		AsyncClient *client;
		std::string HU;  //HU identifier from client
		uint8_t timeout;
	};


	struct THROTTLE
	{
		AsyncClient* toClient;  //associated client
		//int8_t MT;			//multi throttle designator on the client 0-9. -1 signifies ALL
		char MT;		//typically identifiers are M0 through M9 or MT for solo throttles
		char address[8];  //L10293 is the longest poss addr
		int8_t locoSlot;
		uint8_t	MTaction;
		bool orientation;
	//	uint8_t timeout;   //now done at client level
		};

	struct CLIENTMESSAGE
	{
		std::string msg;   //safer inside a vector, will avoid memory leaks
		AsyncClient* toClient;  //pointer to specific client, nullptr=all clients
	};

		
	
	/*function prototypes*/
	void startThrottle(void);
	void seedLoco(void);
	void dumpLoco(void);
	void broadcastChanges(bool clearFlags);
	void broadcastPower(void);
	void broadcastLocoRoster(AsyncClient *client);
	void broadcastTurnoutRoster(AsyncClient *client);
	void sendWiMinimal(AsyncClient* client);  //do i need this?
	void processTimeout();
	uint8_t clientCount(void);




	/*local scope, hence declared static*/
	static int8_t doThrottleCommand(void *data, AsyncClient *client);
	static void sendToClient(char *data, AsyncClient *client);
	static void sendToClient(std::string s, AsyncClient *client);
	static void	queueMessage(std::string s, AsyncClient *client);
	static void setPower(bool powerOn);
	//static int8_t findTurnout(uint16_t turnoutAddress);
	//static int8_t findLoco(char *address, char *slotAddress);   //should move to DCCcore
	static int8_t addReleaseThrottle(AsyncClient *client, char MT, char *address, bool doAdd);
	static bool checkDoSteal(char *address, bool checkOnly, bool &isConsist);
	static void setConsistID(THROTTLE *t);
	static void checkClientID(AsyncClient *client);
	
	

}
#endif
