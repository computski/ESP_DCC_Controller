//
//

/*WiThrottle module
Implements the JRMI WiThrottle protocol
https://www.jmri.org/help/en/package/jmri/jmrit/withrottle/Protocol.shtml
*/

//Uses ESPasycTCP library. This is non blocking inbound and can support multiple ip-clients

/*
 Note ESPasyncTCP can be blocking on a single client if you wish to send a second message.  The fix employed
 here in sendToClient is to send all messages to any given client as one large block message via a message queue

 vector message queue implementing std::string.  This will avoid memory leaks and allow variable length
 messages compared to using char arrays.  vector.clear() will not clean up the memory that the pointers reference.


 Modue supports the following EngineDriver/WiThrottle features;
 loco roster
 turnout roster
 one or more locos (ad hoc consist) on an individual multithrottle (MT) ip-client
 one or more MT per ip-client
 multiple ip-clients
 power status and turnout status changes are broadcast to all clients
 if different MT (either on same or different client) requests a loco, it gets it and takes control
 locking a loco to a single MT, and concept of stealing that loco away
time-out auto-stop if a throttle connection is lost (dead client, dropped Wifi). Set at 5 seconds.

 not supported;
 routes
 momentary function pushbuttons.  They are all toggle at present.

 Tested on:
 Galaxy S3 with EngineDriver
 Galaxy S5 with EngineDriver
 Nexus tablet with EngineDriver
 IOS with WiThrottle lite

 the behaviour of these apps can vary a little as the underlying OS plays a part.  e.g. the S3 does not always connect
 to the server on the first attempt.   WiThrottle lite on IOS does not appear to highlight/lowlight function buttons
 when they are active/deactive.
  */


/*
Auto-stop feature.   This module asks the client to send a heartbeat every 3 seconds.  IOS WiThrottle seems to send one
per second anyway, whereas the Android implementations might take up to 4 seconds.  The module will delcare a timeout
on that client (and all its underlying throttles + locos) at six seconds.  It will send an ESTOP to each of the locos
under that client.

These timeouts occur either because the Wifi signal has dropped out, or you have turned off the mobile phone screen.
With a dropout clearly we have lost control so ESTOP is appropriate.  Turning off the screen I think  sleeps the app and 
whilst it maintains its TCP connection, it no longer sends heartbeats or commands.  ESTOP will then trigger within 6 seconds.

On reconnect or re-wake screen, the EngineDriver or WiThrottle app will attempt to re-establish authority over the locos
it had control of.  The server will support this and it works the majority of the time.  On occasion though, you may need to
exit the app and restart it.

The JRMI protocol only allows one throttle to control a loco at a time.  Because the server does not know if a dropped wifi user
is coming back or not, it holds the throttles associated to that client.   If another client wants them, he needs to invoke
the Steal command.   e.g. user one was driving locos 3 and 4.  he walks out of the building and goes to lunch.  his locos will
ESTOP after 6 seconds.  If he comes back, an hour later he can pick up driving them.  Or user2 could steal them.

It is a feature of this system, that a loco under ED or WiThrottle control can also be controlled from the local hardware unit
think of this as 'dual-control'.  the local hardware display will remain synchronised to the ED throttle.
*/




#include "Global.h"
#include "WiThrottle.h"
#include "DCCcore.h"
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <vector>

using namespace nsWiThrottle;

/*action states for MTaction*/
enum MTACTION {
	MT_NORMAL,
	MT_NEWADD,
	MT_STEAL,
	MT_RELEASE,
	MT_GARBAGE
};

/*use a vector to hold associations between loco slots and MT / clients*/
static std::vector<THROTTLE> throttles;

static std::vector<CLIENTMESSAGE> messages;

static std::vector<CLIENT_T> clients;




#pragma region Test_and_debug

/*for testing this module in isolation, not used in production code*/
void   nsWiThrottle::seedLoco(void) {
	for (int8_t i = 0;i < MAX_LOCO;++i) {
		loco[i].address = 3 + i;
	}
	
	/*do same for turnouts*/
	
	char buffer[9];
	for (int8_t i = 0;i < MAX_TURNOUT;++i) {
		turnout[i].address = i + 128;
		itoa(turnout[i].address, buffer, 10);
		strcpy(turnout[i].name, buffer);
		turnout[i].thrown = false;
	}
}

/*dump the loco array*/
void nsWiThrottle::dumpLoco(void) {
		for (int8_t i = 0;i < MAX_LOCO;++i) {
		Serial.printf("\naddr %d speed %f step %d consist %d flag %d \n", loco[i].address,loco[i].speed, loco[i].speedStep,loco[i].consistID,loco[i].changeFlag?1:0);
	}
	Serial.printf("heap %d \n\n", ESP.getFreeHeap());
	Serial.printf("clients size %d \n\n", clients.size());
	//dump throttle entries
	for (auto throttle : throttles) {
		int sa=0;
		if (throttle.locoSlot >= 0) sa = loco[throttle.locoSlot].address;
		Serial.printf("T-entry-slot %d, slot-addr %d, T-addr %s, MT %d, IP %s \n", throttle.locoSlot, sa, throttle.address, throttle.MT, "no known");

	}

}

/*debug for testing, send minimal response to boot client*/
void nsWiThrottle::sendWiMinimal(AsyncClient* client) {
	if (client->space() > 100 && client->canSend()) {
		char reply[]= "VN2.0\r\nPPA2\r\n";
		//s.toCharArray(reply, 100);
		client->add(reply, strlen(reply));
		client->send();
	}

}



#pragma endregion




#pragma region TCP_client_related



 /* client events */
static void handleError(void* arg, AsyncClient* client, int8_t error) {
	Serial.printf("\n connection error %s from client %s \n", client->errorToString(error), client->remoteIP().toString().c_str());
}


//inbound data from client
static void handleData(void* arg, AsyncClient* client, void *data, size_t len) {
	//2021-01-30 timeout handling. If we see any message from a client, reset its timeout
	//2021-02-03 keep seeing timeouts. Make timeout double the period that the client was instructed to respond on. 
	//2021-09-09 if you have say MAX_LOCO=8 the buffer may not be big enough to capture all incoming data from loco.htm
		for (auto &c : clients) {
			if (c.client == client) {
				//reset the client timeout, timeout is set in seconds, but the timebase is 250mS
				//add 100% margin because ED does not reliably send commands or heartbeat with the 
				//timeout period
				c.timeout = 2 *(4 * WITHROTTLE_TIMEOUT);

			}
		}

		trace(Serial.printf("\ndata from client %s \n", client->remoteIP().toString().c_str());)
	
	/*each chunk of data is terminated with newline so for WiThrottle its a discrete command
	split the incoming data into message elements. delimiter is \n or \r
	Generally WiThrottle only sends multiple elements for consists
	
	2020-12-30 strtok modifies its input char* and this is unsafe because it operates beyond len
	as *data is not null terminated.
	for this reason we copy the data(len) into buffer which is null terminated*/
				
	
	char *msg;
	//assume 512 is large enough for anticipated data
	char buffer[512];
	int cnt = 0;
	memset(buffer, '\0', sizeof(buffer));
	if (len > 511) len = 511;

	//incoming *data was void, need to cast to const char
	strncpy(buffer, (const char*)data, len);
	
	//msg = strtok((char*)data, "\r\n");  //this overruns len of *data because *data is not null terminated
	msg = strtok(buffer, "\r\n");
	while (msg != NULL)
	{

		trace(Serial.printf("element %s\n",msg);)

		if ((msg[0] == 'H') && msg[1] == 'U') {
			//pick up the unique identifier.  BUT if we already have this, it means the prior client
			//is back again but with a new IP.  Problem is we may have disco'd that client already
			//If a client walks out of the building, we'd hold their  client_t and throttles indefinately
			//others would have to request a steal to break any consists and take the locos

			for (auto &c : clients) {
				if (c.client == client) c.HU = msg;
				trace(Serial.printf("NEW ID %s\r\n", msg);)
			}

			//ensure HU identifier is unique
			checkClientID(client);
		}

		//deal with this message element
		//NEW client. Note this might not always be the case, as *heartbeat can case the client to keep emitting its name
		//N is actually the device name and H the device ID, these are always transmitted when a client wants to join
		//but my also be a heartbeat poll.  So strictly we don't want to keep sending the rosters, we need to check if the client exists
		if (msg[0]=='N') {
			//note, no throttle objects are created on the client until the client selects their first loco. so in the meantime
			//on every 10 sec timeout, the client sends Nsomename and we broadcast the turnout roster to all clients
	
			//does this client have any throttle objects yet?
		bool newClient = true;
		for (auto t : throttles) {
			if (t.toClient == client) { 
					newClient = false;
					break;
					}
		}

		//no throttles so send the rosters
		if (newClient) {

			trace(Serial.write("New WiThrottle client");)
	
			broadcastLocoRoster(client);
			broadcastTurnoutRoster(client);
			//2021-01-29 don't send a welcome message, it seems to confuse some clients
			//queueMessage("HMJMRI: Welcome to ESP DCC", client);  
			//send immediately
			broadcastChanges(false);
		}
		else {
			//no need to send anything back as client will next send qR qV
				trace(Serial.println("N");)
			}

		}

		/*CLIENT QUIT*/
		if (msg[0] == 'Q') {
			//WiThrottle has quit, release all locos on this IP client. Engine Driver will release all locos before sending
			//the Q message anyway
			nsWiThrottle::addReleaseThrottle(client, 0xFF, NULL, false);	
			
			//2021-02-03 delete the client_t object from clients vector.  This is a CLEAN exit
			//as opposed to an app crash or wifi dropout
			for (auto it = clients.begin(); it != clients.end(); ) {
				if (it->client == client) {
					it = clients.erase(it);
				}
				else {
					++it;
				}
			}
					   
		}
	

	//HEARTBEAT.  2020-12-30 we don't do anything with this at present
	if (msg[0] == '*') {
				trace(Serial.println(F("heartbeat"));)
	}

	//POWER command
	char *p = strstr(msg, "PPA"); 
	if (p) {
		/*emit new setting. this also handles PPA2 unknown*/
		nsWiThrottle::setPower(msg[3]=='1'?true:false);
	}

	/*TURNOUT command*/
	p = strstr(msg, "PTA");
	if (p) {
		/*will see 2 toggle, C close or T throw. first decode address which is msg[4]*/
		char *a = p + 4;
		if (a) {
			//find matching turnout or assign one
			
			//2021-02-05 WiThrottle can send alphanumeric characters.  This system does not support
			//set-up with a name,  it needs an integer address.  We must validate this
			uint16_t t = atoi(a);

			if (t > 2047) t = 0;
			if (t != 0) {
				//address is in the valid 1-2047 range

				uint8_t i;
				//find a match or assign a new turnout slot
				i = findTurnout(atoi(a));

				switch (p[3]) {
				case 'T':
					turnout[i].thrown = true;
					break;

				case 'C':
					turnout[i].thrown = false;
					break;

				default:
					/*toggle*/
					turnout[i].thrown = !turnout[i].thrown;
				}
				/*set change flag*/
				turnout[i].changeFlag = true;
				trace(Serial.printf("turnout slot %d state %d\n", i, turnout[i].thrown);)
			}
		}
	}

	/*LOCOMOTIVE commands*/
	if (msg[0] == 'M') {
		/*handle loco commands*/
		nsWiThrottle::doThrottleCommand(msg, client);
	}
	
	/*more data?*/
	msg = strtok(NULL, "\r\n");
	}

	
}

static void handleDisconnect(void* arg, AsyncClient* client) {
	//Serial.printf("\n client %s disconnected \n", client->remoteIP().toString().c_str());
	//the client always seems to report as 0.0.0.0 so no point doing anything more than a message
	Serial.println(F("client disconnected"));

	//2021-02-03 take no further action.  Do not delete the client_t element from the clients vector, because we don't know if 
	//this was a clean exit, or a wifi dropout (which causes a connection reset and then a client-disco on reconnect, followed by 
	//a new connect.   given that we want to persist the throttles through a Wifi drop out, we cannot erase the client_t
	//here.  its handed in 'Q' and in checkClientID()
	
	//note, whether or not AsyncTCP deletes the client object subsequently is unknown
}

static void handleTimeOut(void* arg, AsyncClient* client, uint32_t time) {
	Serial.printf("\n client ACK timeout ip: %s \n", client->remoteIP().toString().c_str());
}

static void handleNewClient(void* arg, AsyncClient* client) {
	Serial.printf("\n new client ip: %s", client->remoteIP().toString().c_str());
	// add to list
	CLIENT_T client_t;
	client_t.client = client;
	clients.push_back(client_t);
	
	// register events
	client->onData(&handleData, NULL);
	client->onError(&handleError, NULL);
	client->onDisconnect(&handleDisconnect, NULL);
	client->onTimeout(&handleTimeOut, NULL);
}

/*boot the WiThrottle system*/
void nsWiThrottle::startThrottle(void) {
	AsyncServer *server = new AsyncServer(bootController.tcpPort); // start listening on tcp port
	server->onClient(&handleNewClient, server);
	server->begin();
}

uint8_t nsWiThrottle::clientCount(void) {
	return clients.size();
}


/*send data to a specific client, or all if client=nullptr*/
void nsWiThrottle::sendToClient(char *data, AsyncClient *client) {
	for (auto c : clients) {
		//if no client specified, send to all
		if ((client == nullptr) || (client == c.client)) {
			if (c.client->space() > sizeof(data) && c.client->canSend()) {
				c.client->add(data, strlen(data));
				c.client->send();
			}
		}
	}
}

//send data to a specific client, or all if client=nullptr. overload takes std::string
//std::string::max_size() will tell you the theoretical limit imposed by the architecture your program is running under. 
//internet says ESP will truncate at Ethernet frame buffer len of 1500 bytes
void nsWiThrottle::sendToClient(std::string s, AsyncClient *client) {
	//const char *data = s.c_str(); will cause a crash if you use it to call sendToClient
	//need to copy the data to a new array

	if (s.size() > 0) {
		char *data = new char[s.size() + 1];
		copy(s.begin(), s.end(), data);
		data[s.size()] = '\0';
		sendToClient(data, client);
			
		//we used new to create *data.  delete now else you create a memory leak
		delete data;

		//2021-09-09 instead, would this work?  std::string.data is a const pointer to the data
		//warning: anything that modifies string.data will cause a crash
		//sendToClient(s.data, client);

	}

}

/*check new throttle IDs and deal with duplicates caused by Wifi dropouts*/
void nsWiThrottle::checkClientID(AsyncClient *client) {
	//find the client_t parent to client
	CLIENT_T *cp = nullptr;
	for (auto ct : clients) {
		if (ct.client == client) {
			cp = &ct;
			break;
		}
	}
	if (cp == nullptr) return;

	//HU should be unique.  kill any 'old' one
	for (auto it = clients.begin(); it != clients.end();) {
		//does the new ID match an existing one? and is not self
		if (it->client == cp->client) goto skip;
		if (it->HU.compare(cp->HU) != 0) goto skip;
			//have a match on the 'old' client_t object
			//move all the throttles under it.client over to cp.client
			for (auto &t : throttles) {
				if (t.toClient==it->client) t.toClient = cp->client;
			}
			//delete the old client_t
			it = clients.erase(it);
			return;
		skip:
			++it;
	}
		
	//Note: the only other time a client_t is erased, is on a clean-exit invoked through
	//the Q command.
}

#pragma endregion


#pragma region Throttle_processing

/*add or release locos to/from MultiThrottles on specific ip clients.  MT=0xFF to signifiy all throttles on that client*/
int8_t nsWiThrottle::addReleaseThrottle(AsyncClient* toClient, char MT, char *address, bool doAdd) {
	if (toClient == nullptr) return-1;
	trace(Serial.println("addRT");)

	//re-write.  we flag for release, we don't delete throttle items here
	for (auto& throttle : throttles) {
		if (toClient == throttle.toClient) {
			/*client matches, check address and MT.  MT value of 0xFF signifies ALL*/
			if ((MT == throttle.MT) || (MT == 0xFF)) {
				/*order of precedence and short-circuiting.  if address==NULL we don't want to evaluate next condition
				strcmp because its behaviour with NULL is undefined, i.e. a crash*/

				if ((address == NULL) || (address == "") || (strcmp(address, throttle.address) == 0)) {
					/*found a hit, now for drop, we erase it*/
					if (doAdd) {
						/*bail, item exists we don't wish to add again*/
						return -1;
					}
					else
					{
						/*flag item for release and subqeuent deletion*/
						throttle.MTaction = MT_RELEASE;

						/*2020-05-26 remove the consistID*/
						if (throttle.locoSlot >= 0 && throttle.locoSlot < MAX_LOCO) {
							loco[throttle.locoSlot].consistID = 0;
						}
					}
				}
			}
		}
	}//loop for throttles


	/*exit now if delete mode, as we have flagged all necessary throttle items.
	For add mode, we have confirmed a new entry is required.*/

	if (!doAdd) return -1;

	trace(Serial.println("addRT2");)

	/*adding a throttle.   It is assumed 'safe' to do so, i.e. doCheckSteal was called prior*/
	THROTTLE myT;
	strncpy(myT.address, address, 7);
	myT.MT = MT;
	myT.toClient = toClient;
	/*find matching loco slot, or assign one*/
	myT.locoSlot = findLoco(address, NULL);
	/*flag as a new assignment*/

	myT.MTaction = MT_NEWADD;
	trace(Serial.println("addRT3");)

	/*add item if valid slot found, i.e. positive slot value*/
	if ((myT.locoSlot >= 0) && (myT.locoSlot < MAX_LOCO)) {
		//2020-05-26 assign a consistID to underlying loco slot
		nsWiThrottle::setConsistID(&myT);
		throttles.push_back(myT);
		/*UPDATE the appropriate loco array element.*/
		/*Note: Engine Driver expects to pick up an existing loco from a roster, which predfines the speed steps
		 *ED cannot send a message to set 28/128 steps.  it appears to work natively in 128 mode
		 *So, if the loco was not defined in the local UI, we just have to leave the use128 setting as is on the slot*/
		loco[myT.locoSlot].address = atoi(address + 1);
		loco[myT.locoSlot].useLongAddress = address[0] == 'L' ? true : false;
		/*flag a change, this will cause the existing values to transmit and get picked up by the MT*/
		loco[myT.locoSlot].changeFlag = true;
		//2021-1-15 flag the roster has changed
		bootController.flagLocoRoster = true;
	}
	trace(Serial.println("addRT4");)

	return myT.locoSlot;
}

/*process wiThrottle loco commands against specific toClient, return -1 if fail*/
int8_t nsWiThrottle::doThrottleCommand(void *data, AsyncClient *toClient) {
	//new message comes in over a client/throttle,this might have an impact on multiple outgoing throttles.
	if (data == NULL) return -1;
	if (toClient == nullptr) return -1;

	char* msg = (char*)data;
	int8_t i;

	/*doesn't matter if we are using a literal DCC address entry or a roster entry, we can index off the loco address*/

	char *p = strstr(msg, "<;>");   //returns <;>and characters to right
	if (!p) { return -1; }  //didn't find <;> means this is not a valid command


	/*STEAL. S denotes client wishes to proceed with a steal operation MTSL341<;>L341*/
	if (msg[2] == 'S') {
		char address[10];
		memset(address, '\0', sizeof(address)); //pad with nulls

		//eg command was MTSL341<;>L341  so address is msg+3 to p-msg
		strncpy(address, msg + 3, (p - msg) - 3);  //returns the loco address, L341 in example
		bool isConsist;
		/*proceed with the steal operation*/
		nsWiThrottle::checkDoSteal(address, false, isConsist);
		/*and assign the loco to requesting client*/

		/*2020-11-25 expect a numeric throttle value, but T denoting a single throttle is also valid and will map to 36d ='T'-'0'*/
		return nsWiThrottle::addReleaseThrottle(toClient, msg[1], address, true);
	}

	/*ADD. + denotes add loco to a multithrottle. Note when we add, we should set a flag on the impacted slot
	thus forcing its speed, direction and function flags to be issued to the new MT*/
	if (msg[2] == '+') {
		/*whether we are adding a pure DCC address, or adding from roster, the first part of string is the dcc address*/
		/*so hunt for a slot on this basis*/

		char address[10];
		memset(address, '\0', sizeof(address)); //pad with nulls

		//eg command was MT+S83<;>  so address is msg+3 to p-msg
		strncpy(address, msg + 3, (p - msg) - 3);  //returns the loco address, L83 in example
		/*0 or +ve return values will result in an echo of the message back to client confirming the ADD action*/

		bool isConsist;
		char slotAddress[10];

		if (findLoco(address, slotAddress) < 0) {
			/*bail, no slots available*/
			trace(Serial.println("##1");)
			return -1;
		};

		/*slotAddress contains address if we don't need to overwrite an existing slot address, else it
		returns the existing loco address in the slot we are about to bump*/
		THROTTLE myT;

		/*check if we need to steal from another MT? We use the slotAddress, which will match address if the slot is already
		in use, or will be the existing slotaddress if we need to bump one.*/
		if (nsWiThrottle::checkDoSteal(slotAddress, true, isConsist)) {
			/*a steal is required from an existing MT*/
			myT.locoSlot = -1; //no slot assigned
			myT.MTaction = MT_STEAL;
			/*set up the steal command with the actual address we want*/
			/*e.g. if all slots are full and we want to add L999, then we may need to bump S3 from another throttle
			but the steal command itself will relate to L999*/
			strncpy(myT.address, address, 7);
			myT.MT = msg[1];
			myT.toClient = toClient;
			throttles.push_back(myT);

		}
		else
		{/*no steal required, proceed.  if we are taking a slot for say S77 that was not
		 under any MT control, we can proceed.*/
			return nsWiThrottle::addReleaseThrottle(toClient, msg[1], address, true);

			/*2021-01-15.  addReleaseClient will bump old locos off the roster, it is also aware it is changing the roster
			and sets the flagLocoRoster accordingly*/
			
		}
		return -1;
	}

	/*RELEASE minus symbol will release loco from MT  M0-*<;>r
	note that Engine Driver also sends M0*A<;>V0 just prior to the release command
	Apple WiT sends MT-L1234<;>r*/
	if (msg[2] == '-') {
		/*we leave speed and direction as is*/
		/*special return code 99 indicates success and will trigger a command echo, but its not a loco slot*/
		nsWiThrottle::addReleaseThrottle(toClient, msg[1] , NULL, false);
		
		return 99;
	}


	/*FUNCTION or SPEED command processing  M0A*<;>
	 but we need to provide ad-hoc consist support hence need to find all matching slots
	 for the given throttle*/

	 /*NOTE: we should process for all slots which match (consist) not just the first*/
	bool changeFlag = false;
	char address[10];
	memset(address, '\0', sizeof(address)); //pad with nulls

	//eg command was MT+L83<;>  so address is msg+3 to p-msg
	strncpy(address, msg + 3, (p - msg) - 3);  //returns the loco address, L83 in example

	//2020-05-20 need to process as * or individual addresses.  this is important for consists
	//as speed is sent using * denotion and direcion is specific to individual locos in the consist

	//2020-11-24 find highest history value
	uint16_t age = 0;
	for (auto h : loco) {
		if (h.history > age) age = h.history;
	}


	for (auto throttle : throttles) {
		/*matches our target client*/
		if (throttle.toClient != toClient) { continue; }
		/*yes, how about the MT id?*/
		if (msg[1]  != throttle.MT) { continue; }
		/*points to a valid loco slot?*/
		if (throttle.locoSlot < 0) { continue; }

		LOCO &loc = loco[throttle.locoSlot];

		/*We have confirmed matching client and same MT, now process specific commands*/
		/*M0A*<;>V22 or M0AS23<;>R0 for example. 'A' signifies a loco action.*/
		/*p points at the <;>command part*/

		//2020-01-29 also see M0A*<;>qV and qR which are queries for values held on server
		if (msg[2] == 'A') {
			if (p[3] == 'V' && (msg[3] == '*' || (strcmp(address, throttle.address) == 0))) {
				/*SPEED command, respond to * or match address exactly */

				int8_t speedCode = atoi(p + 4);

				trace(Serial.printf("speed cmd %d\\r\n", speedCode);)

				if (speedCode < 0) {
					/*handle estop*/
					loc.speed = 0;
					/*set the timer, this tells the packet engine to send the estop code*/
					loc.eStopTimer = LOCO_ESTOP_TIMEOUT;
					loc.changeFlag = true;
					changeFlag = true;
					continue;
				}

				/*it seems WiThrottle works with 126 speed steps natively*/
				loc.speed = speedCode / 126.0;
				/*deal with 128 step codes*/
				if (loc.use128)
				{
					loc.speedStep = speedCode;
					loc.speed = speedCode / 126.0;
					loc.changeFlag = true;
					changeFlag = true;
					loc.history = ++age;
					continue;
				}

				/*else calculate 28 step display code.
				2020-05-27 correct and simplified. max speed is display code 28*/
				loc.speedStep = 28 * loc.speed;
				loc.changeFlag = true;
				changeFlag = true;
				loc.history = ++age;
				continue;


			}

			//2021-01-29 handle <;>qR and <;>qV
			if (p[3] == 'q' && (msg[3] == '*' || (strcmp(address, throttle.address) == 0))) {
				//cient asking for current direction R or velocity V.
				//all we need do is set the changeFlag on the corresponding loco slots, the regular
				//broadcastChanges will send out the message
	
				if ((p[4] == 'R')|| (p[4] == 'V') ){
					//need to queue a response message M0AL341<;>V23 or M0AL341<;>R1 for example
					for (auto t : throttles) {
						if (t.toClient == toClient) {
							//don't set directionFlag, this would cause a direction toggle (used in consists)
							loco[t.locoSlot].changeFlag = true;
							//send the function settings for good measure
							loco[t.locoSlot].functionFlag = true;
						}
					}
				}
			}


			/*Idle command MT0A<;>I or MTA*<;>I for example.  set throttle to zero speed  MTAS6<;>I*/
			/*M0A*<;>V22 or M0AS23<;>R0 for example. 'A' signifies a loco action.*/
			/*so the format is differen for solo throttles.  these are MTAS6<;>I as opposed to MOA*<;>I
			but i suppose M0A*<;>I is also valid
			p[3] will point at I  msg[3] will be S*/
			if (p[3] == 'I' && (msg[3] == '*' || (strcmp(address, throttle.address) == 0))) {

				trace(Serial.printf("Idle command\r\n");)
				loc.speed = 0;
				loc.speedStep = 0;
				loc.changeFlag = true;
				loc.eStopTimer = LOCO_ESTOP_TIMEOUT;
				changeFlag = true;
				loc.history = ++age;
				continue;	 			
			}
			
			/*ESTOP emergency stop command X  element MTAS6<;>X*/
			if (p[3] == 'X' && (msg[3] == '*' || (strcmp(address, throttle.address) == 0))) {
				trace(Serial.printf("ESTOP command\r\n");)
				for (auto &L : loco) {
					L.speed = 0;
					L.speedStep = 0;
					L.changeFlag = true;
					changeFlag = true;
					//2021-01-29 no need to display eStop on the local UI
					//all speeds will go to zero and power is not shut off
				}
				continue;
			}


			/*FUNCTION commands*/
			if (p[3] == 'F') {
				/*function command M0A*<;>F012 where first char after F 0|1 is state, of f12 in this example */
				/*loco[].function is 16 bit mapped functions*/
				uint8_t b = atoi(p + 5);

				trace(Serial.printf("func cmd %d\r\n", b);)

#ifdef NOT_USED				
				/*this is immediate tracking of finger-on-button state*/
				if (p[4] == '1') { loc.function |= (1 << b); }
				else
				{
					loc.function &= ~(1 << b);
				}
#endif					
				/*2020-04-25 revised.  if we see keydown, toggle state*/
				/*we ignore keyup, however it would be possible to set a timer and if user holds down for >1sec
				then we obey keyup rather than leaving state as set*/
				if (p[4] == '1') { loc.function ^= (1 << b); }

				loc.functionFlag = true;
				loc.history = ++age;
				changeFlag = true;
				continue;
			}//function set

			/*DIRECTION command M0A*<;>R0
			actually throttle seems to send as one or more loco-address commands
			M0AS5<;>R0  this is because locos in a consist may be facing in opposite directions*/
			if (p[3] == 'R') {
				if (msg[3] == '*' || (strcmp(address, throttle.address) == 0)) {
					/*R0 indicates reverse, R1 or anything else is forward*/
					loc.forward = (p[4] == '0') ? false : true;
					loc.changeFlag = true;
					loc.directionFlag = true;
					changeFlag = true;
					loc.history = ++age;
				}
				continue;
			}

		}// A-test

	}//loop for throttle


	/*we have executed the command on one or more slots*/
	if (changeFlag) { return 99; }
	return -1;
	/*net outcome is all loco slots which were impacted by the command will have their changeFlag or functionFlag set
	these all need to be broadcast and the local UI updated*/

}


/*return true if steal required and can also indicate that address is part of consist*/
bool nsWiThrottle::checkDoSteal(char *address, bool checkOnly, bool &isConsist) {
	int c = 0;
	int c_max = 0;
	/*for a given loco address, find which MT(s) it is on, and then for a given MT find how many other entries that MT has*/
	/*if not checkOnly, we set the slot references to -1 to force that throttle to release its loco(s)*/

	trace(Serial.printf("chkDS %d\r\n", throttles.size());)
	for (auto throttle : throttles)
	{
		c = 0;
		/*outer loop, find all instances of that addr on a MT.  should only be one*/
		if (strcmp(throttle.address, address) == 0) {
			/*found a hit, now check whether this MT on this client has >1 slot*/

			/*inner loop, for this particular throttle is it running a consist?*/
			for (auto& th : throttles) {
				/*confirm its our target throttle.  Note we will find-self as one of these*/
				if ((th.toClient == throttle.toClient) && (th.MT == throttle.MT)) {
					c++;
					/*force throttle to release its loco(s)*/
					if (!checkOnly) { th.MTaction = MT_RELEASE;
					trace(Serial.println(F("##7 force release\r\n"));)
					}
				}
			}
			if (c > c_max) c_max = c;
		}
	}

	isConsist = (c_max > 1) ? true : false;
	return (c_max == 0) ? false : true;
}


void nsWiThrottle::setPower(bool powerOn) {
	if (powerOn) {
		/*turn on track power*/
		power.trackPower = true;
		power.trip = false;
		//2021-10-27 also zero all loco speeds in anticipation of power being restored
		//broadcasting of this is dealt with elsewhere
		for (auto &loc : loco) {
			loc.speed = 0;
			loc.speedStep = 0;
			loc.changeFlag = true;
		}
	}
	else {
		/*turn off track power*/
		power.trackPower = false;
		power.trip = false;
		
	}
	nsWiThrottle::broadcastPower();
}

void nsWiThrottle::broadcastPower(void) {
	if (power.trackPower) {
		queueMessage("PPA1\r\n", nullptr);
	}
	else {
		queueMessage("PPA0\r\n", nullptr);
	}
}

/*queue a message for a specific client, or all if client=nullptr*/
void nsWiThrottle::queueMessage(std::string s, AsyncClient *client) {
	CLIENTMESSAGE m;
	m.toClient = client;
	m.msg = s;
	messages.push_back(m);
}

/*a consist ID is assigned to indicate a loco slot is tied to a WiThrottle.  It is also used to manage ad-hoc consists on a throttle*/
void nsWiThrottle::setConsistID(THROTTLE *t) {
	if (t == nullptr) return;
	if (t->locoSlot<0 || t->locoSlot>MAX_LOCO) return;
	trace(Serial.println("setCID");)

	/*scan throttles, is this a consist and does it have an ID?*/
	for (auto throttle : throttles) {
		if (throttle.toClient != t->toClient) continue;
		if (throttle.MT != t->MT) continue;
		if (throttle.locoSlot == t->locoSlot) continue;  //ignore self
		/*found a loco in this throttle consist, pick up the consistID*/
		if (loco[throttle.locoSlot].consistID != 0) {
			/*this member is carrying the ID, apply it*/
			loco[t->locoSlot].consistID = loco[throttle.locoSlot].consistID;
			return;
		}
	}

	//reach here if no consist exists, i.e. first member, so allocate a consistID
	uint8_t high = 0;
	for (auto loc : loco) {
		if (loc.consistID > high) { high = loc.consistID; }
	}
	loco[t->locoSlot].consistID = ++high;
   
}


/*send out the current loco roster to all clients, also sends throttle-boot preamble and power status*/
void nsWiThrottle::broadcastLocoRoster(AsyncClient *client) {
	CLIENTMESSAGE m;
	m.toClient = client;   //if nullptr will send to all clients
	
	//JRMI Version 2.0, and set WITHROTTLE_TIMEOUT which is defined in the header file
	//Note that *6 is from server to client. client needs to respond *+ or *- to activate/deactivate heartbeat monitoring
	//2021-12-01 added PW80 to indicate webserver is on port 80
	//2021-12-01 also added HTDCCESP as a server message to the client.  Steve Todd, the author of Engine Driver has
	//kindly agreed to recognise this token in his app so that the Web menu item appears as DCC ESP
	char buff[30];
	snprintf(buff, 29, "VN2.0\r\nPW80\r\n*%d\r\nDCCESP\r\n", WITHROTTLE_TIMEOUT);
	m.msg = buff;
	
	//example 2 entry roster list RL2]\[RGS 41}|{41}|{L]\[Test Loco}|{1234}|{L
	
	if (power.trackPower) {
		m.msg.append("PPA1\r\nRL");
	}
	else
	{
		m.msg.append("PPA0\r\nRL");
	}

	/*calculate roster count now, as its easier to append to string as we go*/
	int8_t rosterCount = 0;
	for (auto loc : loco) {
		if (loc.address != 0) {
			++rosterCount;
		}
	}

	char buffer[20];
	itoa(rosterCount, buffer, 10);
	m.msg.append(buffer);

	//roster example, 2 entries  
	// RL2
	// ]\[state rl }|{8005 }|{L
	// ]\[class 70 }|{7003 }|{L
	
	//send non-zero loco slots
	for (auto loc : loco) {
		if (loc.address != 0) {
			itoa(loc.address, buffer, 10);
			m.msg.append("]\\[");  //escape the backslash
			//loco name comes first. If name is null, use address. There's a bug in EngineDriver which means
			//locos with a null name sometimes do not display in the roster.
			if (loc.name[0] == '\0') { 
				m.msg.append(buffer); }
			else {
				m.msg.append(loc.name);
			}
			m.msg.append("}|{");
			//loco address
			m.msg.append(buffer);
			m.msg.append("}|{");
			if (loc.useLongAddress) {
				m.msg.append("L");
			}
			else { m.msg.append("S"); }
		}
	}

	m.msg.append("\r\n");
	messages.push_back(m);
}

/*send current turnout roster to all clients*/
void nsWiThrottle::broadcastTurnoutRoster(AsyncClient *client) {
	CLIENTMESSAGE m;
	m.toClient = client;  //if nullptr send to all clients
	char buffer[8];

	//first string defines the states and their numeric equivalents
	//PTT]\[Turnouts}|{Turnout]\[Closed}|{2]\[Thrown}|{4
	
	m.msg.append("PTT]\\[Turnouts}|{Turnout]\\[Closed}|{2]\\[Thrown}|{4\r\n");

	//second string defines the turnouts themselves as an array. There's no count, whereas for locos there is.
	//PTL]\[LT12}|{Rico Station N}|{1]\[LT324}|{Rico Station S}|{2
	//PTL]\[512}|{512}|{4]\[513}|{513}|{2  should work.
	//in this context we are sending absolute state of those turnouts either 2 or 4
	
	m.msg.append("PTL");
	for (auto turn : turnout) {
		if (turn.address != 0) {
			m.msg.append("]\\[");  //escape the backslash
			itoa(turn.address, buffer, 10);
			m.msg.append(buffer);
			m.msg.append("}|{");
			//2021-02-07 if name is null, it will fail to display in ED, use the address instead
			if (turn.name[0] == '\0') {	m.msg.append(buffer);}
			else 
			{ m.msg.append(turn.name); }

			if (turn.thrown) {
				m.msg.append("}|{4");
			}
			else
			{
				m.msg.append("}|{2");
			}
		}
	}
	m.msg.append("\r\n");
	messages.push_back(m);

}

/*sub-processing routine for broadcastWiChanges*/
void queueTurnouts(bool clearFlags) {
	std::string s;
	char buf[20];
	for (auto& turn : turnout) {
		if (!turn.changeFlag) { continue; }
		memset(buf, '\0', sizeof(buf)); //pad with nulls
		trace(Serial.printf("queue turnouts %d state%d\n", turn.address, turn.thrown);)
		//send absolute state PTAxAddr
		if (turn.thrown) {
			sprintf(buf, "PTA4%d\r\n", turn.address);
		}
		else {
			sprintf(buf, "PTA2%d\r\n", turn.address);
		}
		s.append(buf);
		if (clearFlags) { turn.changeFlag = false; }
	}//turnout loop

	 //send msg to ALL clients
	queueMessage(s, nullptr);
}

/*builds a datagram per client from the loco and turnout changes and any queued messages. Call regularly from main loop.*/
void nsWiThrottle::broadcastChanges(bool clearFlags) {
	//sending to a specific client is blocking if you attempt to send more data before the first transmission
	//has completed.  for this reason we need to group all messages per client and send as a jumbo message

		//deal with turnout changes, put these in the message queue
	queueTurnouts(clearFlags);
	
	//2021-02-01 deal with turnout roster AND loco roster changes
	if (bootController.flagTurnoutRoster) {
		broadcastTurnoutRoster(nullptr);
		if (clearFlags) bootController.flagTurnoutRoster = false;
	}
	//and loco roster
	if (bootController.flagLocoRoster) {
		broadcastLocoRoster(nullptr);
		if (clearFlags) bootController.flagLocoRoster = false;
	}

	std::string m;
	char buf[32];
	char myT[4];  //target throttle 

	//loop for client; we build messages on a per-client basis
	for (auto c : clients) {
		//clear msg
		m.clear();

		//loop through all throttles per specific client, then for each loco found thereunder, add it to
		//the block message
		for (auto& throttle : throttles) {
			//2020-11-25 special case for solo throttles, these are stored as 36d but need to be emitted as T
			memset(myT, '\0', sizeof(myT));
			
		
			//2021-02-01 .MT is a single char, can be alpha numeric, was captured as an ascii code originally
			myT[0] = throttle.MT;

			if (c.client == throttle.toClient) {
				//client matches.  we need to transmit all MTs on this client if they have changes
				bool isConsistent = false;

				//does loco[].address no longer	match throttle.address?  In which case, issue a release command to the MT

				if (throttle.locoSlot < 0 || throttle.locoSlot >= MAX_LOCO) {
					//loco slot not valid, isConsistent=false
					isConsistent = false;
				}
				else {
					sprintf(buf, "S%d", loco[throttle.locoSlot].address);
					if (loco[throttle.locoSlot].useLongAddress) {
						buf[0] = 'L';
					}
					isConsistent = (strcmp(buf, throttle.address) == 0) ? true : false;
				}

				//process the required action on the throttle
				switch (throttle.MTaction) {
				case MT_NORMAL:
					if (loco[throttle.locoSlot].changeFlag == false) { break; }
					if (!isConsistent) {
						//flag inconsistent data throttles for release
						throttle.MTaction = MT_RELEASE;
						break;
					}
					//Normal operation is send speed and direction
					sprintf(buf, "M%sA%s<;>V%d\r\n", myT, throttle.address, uint8_t(126 * loco[throttle.locoSlot].speed));
					//test for estop - yet to implement
					m.append(buf);

					if (loco[throttle.locoSlot].forward) {
						sprintf(buf, "M%sA%s<;>R1\r\n", myT, throttle.address);
					}
					else {
						sprintf(buf, "M%sA%s<;>R0\r\n", myT, throttle.address);
					}
					m.append(buf);
					break;

				case MT_NEWADD:
					// MT+addr<;>addr
					if (!isConsistent) {
						//flag for release
						throttle.MTaction = MT_RELEASE;
						break;
					}
					//send add instruction MT+addr<;>addr, this works whether adding from the roster or adding a DCC address direct
					sprintf(buf, "M%s+%s<;>%s\r\n", myT, throttle.address, throttle.address);
					m.append(buf);
					//now send speed and dir
					sprintf(buf, "M%sA%s<;>V%d\r\n", myT, throttle.address, uint8_t(126 * loco[throttle.locoSlot].speed));
					//test for estop - yet to implement
					m.append(buf);

					if (loco[throttle.locoSlot].forward) {
						sprintf(buf, "M%sA%s<;>R1\r\n", myT, throttle.address);
					}
					else {
						sprintf(buf, "M%sA%s<;>R0\r\n", myT, throttle.address);
					}
					m.append(buf);

					throttle.MTaction = MT_NORMAL;
					break;

				case MT_STEAL:
					//steal message is not based on the slot addr, rather the throttle addr MTSaddr<;>addr
					sprintf(buf, "M%sS%s<;>%s\r\n", myT, throttle.address, throttle.address);
					m.append(buf);
					//steal throttle placeholder has served its purpose, delete it 
					throttle.locoSlot = -1;
					throttle.MTaction = MT_GARBAGE;
					break;

				case MT_RELEASE:
					//MT-addr<;>addr
					sprintf(buf, "M%s-%s<;>%s\r\n", myT, throttle.address, throttle.address);
					m.append(buf);
					//tag the slot for garbage collection
					throttle.locoSlot = -1;
					throttle.MTaction = MT_GARBAGE;
					break;

				case MT_GARBAGE:
					//do nothing, garbage collector is at end of routine
					break;
				}//switch for loco changes


				//process any function changes on this throttle
				if (isConsistent && loco[throttle.locoSlot].functionFlag) {
					//for function, can shorten base to be MTA* instead of full loco address

					/*2020-11-25 that may not work for single throttles.  may have to send the full loco addr. BUG*/
					int8_t fState = 0;
					for (int f = 0;f < 17;f++) {
						//APPEND to msg
						fState = ((loco[throttle.locoSlot].function & (1 << f)) == 0) ? 0 : 1;
						sprintf(buf, "M%sA*<;>F%d%d\r\n", myT, fState, f);
						m.append(buf);
					}
				}//function
			}//ip client match
		}//loop for throttle

		//2020-11-27 append any client specific message
		for (auto g : messages) {
			if ((g.toClient == nullptr) || (g.toClient == c.client)) {
				m.append(g.msg);
			}
		}
		
		//have built a jumbo message on a per client basis, send this

trace(
		if (m.length() > 0) {
			Serial.println(F("\nWIT BCAST"));
			Serial.printf("%s\n", m.c_str());
			Serial.println(F("WIT BCAST END"));
		}
)
		sendToClient(m, c.client);

	}//client loop


	//done with all message processing
	messages.clear();
	
	//done with all processing on all throttles and all clients.  Only now can we clear flags at loco-slot level
	if (clearFlags) {
		for (auto& loc : loco) {
			loc.changeFlag = false;
			loc.functionFlag = false;
		}
	}

	
	//lowest priority is garbage collection.  Delete any throttles tagged as garbage
	for (auto it = throttles.begin(); it != throttles.end();) {
		if (it->MTaction == MT_GARBAGE) {
			it = throttles.erase(it);
			trace(Serial.println(F("garbage"));)
		}
		else
		{
			++it;
		}
	}
}

/*call every second from main loop, will send estop to locos who's throttles have timed out*/
void nsWiThrottle::processTimeout() {
	//2021-1-29 client based.  Only process the timeout event once as we hit zero

	for (auto &c : clients) {
		//loop per client
		if (c.timeout == 0) continue;
		c.timeout -= c.timeout > 0 ? 1 : 0;
		if (c.timeout == 0) {
			//have timed out. stop all throttles under this client.
			//the throttles are not deleted and remain associated with the HU identifier of the client
			//if the client reconnects, we then use the HU to re-associate with the throttles.

			for (auto &t : throttles) {
				if (t.toClient == c.client) {
					loco[t.locoSlot].speed = 0;
					loco[t.locoSlot].speedStep = 0;
					loco[t.locoSlot].changeFlag = true;
					//do not flag for garbage collection
					//t.MTaction = MT_GARBAGE;
					trace(Serial.printf("clnt timeout %d\r\n", loco[t.locoSlot].address);)
				}
				
			}


		}
		
	}
}



#pragma endregion