// 
// 
// 

#include "JsonThrottle.h"
#include "DCCcore.h"


//2020-04-25 need to re-write to tidy up broadcast in conjunction with WiThrottle
//also need to add extra level of nesting for ESP custom params I have created



//need to include DCCcore.h as we need the functions in there.  Global.h does not take care of this
//at present because the module includes are in the .ino file

/*2020-03-30 have moved all websocket & json code to this module.  the DCCcore state engine is exposed through
its header and is availble here

2020-5-28 DigiTrains implements double-header but not very well.  You can elect several locos to be in a double header
but they don't mirror one another's speed or direction on the display, even though setting a speed on one of them results in
a consistent speed setting for both being transmitted.  To avoid confusion on the DigiTrains display you should only display
the lead loco.  Digitrains does not respond to incoming WS data so you cannot use this to update its display.   I consider WiThrottle
to be superior.

If you use Digitrains ad-hoc consists, there is no way for the ESP controller to become aware two locos are in a consist.
With digitrains, it is also possible to send data to the controller for a loco that cannot be given a slot if all slots are 
in use.  e.g. say 3-4-5-6 are in use. Digi can send 7 to the ESP.  The ESP won't be able to allocate a slot, and therefore
will not transmit any commands to line.  Digi provides no means of letting the user know that 7 is a 'dead stick'.



Deserialising is easy, you specify elements off the root
{"type":"turnout", "data" : {"name":"IT99", "state" : 0}}
		root["address"], root["data"]["name"]

Serialising is 
DynamicJsonBuffer jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();
	JsonObject& data = root.createNestedObject("data");
then write to root["type"] and data["name"]

This module makes use of the Arduino string class, which is not recommended.  To use cstring requires us to 
predefine large buffers and use strcpy() to populate with the incoming json objects.

Alternatively we could use std::string
*/


using namespace nsJsonThrottle;


#define TRACE


void nsJsonThrottle::sendWSmessage(char* msg) {
	//send json formed message back over websocket
}

void nsJsonThrottle::processJson(JsonObject& root) {
//process incoming json

	const char *sType= root["type"];
	if (sType == nullptr) return;


	/*caution: payload will be modified by the parse call.  *payload is a pointer to the memory location and this gets modified so we have to
	copy all those locations first.  https://www.cs.bu.edu/teaching/cpp/string/array-vs-ptr/*/



	/*jrmi turnout messages
	 *{"type":"turnout","data":{"name":"IT99","state":0}}
	 *http://jmri.sourceforge.net/help/en/html/web/JsonServlet.shtml
	 *https://www.jmri.org/JavaDoc/doc/jmri/server/json/package-summary.html
	 *this routine only responds if the name is a numeric 11-bit address
	 *the reason is the DigiTrains message does not contain the address, only the turnout name
	 *and the reason for this is you are supposed to pull a turnout roster from the server
	 *that said, the general loco commands carry the address... so much for consistency*/

	if (strcmp(sType, "ping")==0) {
		/* we should respond "pong". have not implemented*/
	}


	/*roster lists {"type":"list", "list" : "turnouts"}*/
	if (strcmp(sType, "list")==0) {
		_broadcastJSONroster(root["list"]);
		return;
	}



	if (strcmp(sType, "turnout")==0) {
		int8_t k = 31 + _setJSONturnout(root["address"], root["data"]["name"], root["data"]["state"]);
		return;
		/*2020-04-28 need to set a flag and then handle broadcasts centrally*/

	}

	if (strcmp(sType, "service")==0) {
		/*Programming On the Main implementation*/
		_setJSONcv(root["cv"], root["val"], root["mode"]);
		return;
	}

	if (strcmp(sType, "power")==0) {
		/*can enable/disable track power, set service power or set a new current limit
		 *if unit has tripped, sending power.state=4 will re-enable*/
		_setJSONpower(root["data"]["state"], root["ESP"]["setCurrentLimit"], root["ESP"]["report"]);
		return;


	}
	/*jrmi throttle messages
	 * {"type":"throttle","data":{"throttle":"CSX754","address":754,"speed":0.25,"forward":true},"ESP":{"step":7 ,"maxStep":128, "useLong":true}}
	address is part of a throttle message, but speed is a float percentage.
	ESP values deal with current speed step and max steps as well as identifying if the address is short or long.
	*/
	if (strcmp(sType, "throttle")!=0) { return; }
#ifdef TRACE
	Serial.println("JSON throttle inbound");
#endif
	/*when using jrmi throttle, the slider sends speed but then follows this with direction command.  this is prob missing from the slider command*/
	/*function instructions are recceived as root["data"]["F0"] etc. so we have to look for each and then read its value*/

	/*very inefficient, we are passing 13 true/false/blank strings*/
	String _funcArray[13];

	for (uint8_t i = 0;i < 13;i++) {
		String f = "F";
		f = f + i;
		/*Json parser cannot pass a string directly to an array of strings*/
		String g = root["data"][f];

		_funcArray[i] = g;
	}

	/*last two params are steps and nudge, neither are supported by JMRI throttle*/
	//int8_t setJSONthrottle(String address, String speed, String direction, String func[], String throttle, String step, String maxStep){

	int k = _setJSONthrottle(root["data"]["address"], root["data"]["speed"], root["data"]["forward"], _funcArray, root["data"]["throttle"], root["ESP"]["step"], root["ESP"]["maxStep"], root["ESP"]["useLong"]);

	//DEBUG.  dump the line packets from the packet engine
	//loco[k].debug = true;



};








/*broadcast routines*/


/*send throttle details for slot k, with augmented data*/
void  nsJsonThrottle::_broadcastJsonThrottle(int k) {
	DynamicJsonBuffer jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();
	JsonObject& data = root.createNestedObject("data");
	JsonObject& ESP = root.createNestedObject("ESP");

	root["type"] = "throttle";
	//{"type":"throttle","data":{"address":754,"speed":0.0,"forward":true, "F0":false, "throttle":"CSX754"}}


	//should become
	//{"type":"throttle","data":{"address":754,"speed":0.0,"forward":true, "F0":false, "throttle":"CSX754"},
	//"ESP":{"step":0,"maxStep":128,"useLong":true}}

	//note throttle is now the wiThrottle name.  the loco 'name' will need to be added to the struct
	//unless throttle is used by JsonThrottle?


	if ((k > -1) && (k < MAX_LOCO)) {
		data["address"] = loco[k].address;
		data["forward"] = loco[k].forward;
		data["speed"] = loco[k].speed;
		data["throttle"] = loco[k].name;
		/*nudge is an inbound only commanded value. Prepare F values if these changed*/
		/*ESP node with augmented values*/
		ESP["step"] = loco[k].speedStep;  //actual speed step instr
		if (loco[k].use128) { ESP["maxStep"] = 128; }
		else { ESP["maxStep"] = 28; }
		ESP["useLong"] = loco[k].useLongAddress;

		/*add loco F0, F1 etc to data node*/
		if (loco[k].functionFlag) {
			for (int8_t i = 0;i < 13;i++) {
				String f = "F";
				f = f + i;
				data[f] = (((loco[k].function >> i) & 0x01) == 0x01);
			}
		}
	}
	else {
		/*fail, no slot*/
		ESP["error"] = "no slot";
	}
	root.printTo(Serial);
	String out;
	root.printTo(out);
	
	
	
	//sendWSmessage(out);
	//webSocket.broadcastTXT(out);

}


/*turnout example broadcast
 *{"type":"turnout","data":{"name":"IT99","state":4},ESP{"address":22}}
 *2020-05-18 modified, call with turnout slot, we no longer use an offset of 31
 */

void nsJsonThrottle::_broadcastJsonTurnout(int8_t k) {
	Serial.printf("doBroadcastTurnout %d\n", k);
	if (k < 0 || k >= MAX_TURNOUT) { return; }
	DynamicJsonBuffer jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();
	JsonObject& data = root.createNestedObject("data");
	JsonObject& ESP = root.createNestedObject("ESP");

	root["type"] = "turnout";
	data["name"] = turnout[k].name;
	data["state"] = turnout[k].thrown ? 4 : 2;
	ESP["address"] = turnout[k].address;
	String out;
	
	/*can also use a char array to avoid arduino strings*/
	root.printTo(out);
	//webSocket.broadcastTXT(out);
}





/*broadcast power status, augmented JMRI
https://github.com/bblanchon/ArduinoJson/issues/264
*/
void  nsJsonThrottle::broadcastJsonPower(void) {
	if (!power.report) { return; }

	StaticJsonBuffer<200> jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();
	JsonObject& data = root.createNestedObject("data");
	JsonObject& ESP = root.createNestedObject("ESP");

	root["type"] = "power";
	/*JMRI state:4 is power off */
	data["state"] = power.trackPower ? 2 : 4;
	
	/*Augmentations under the ESP node*/
	ESP["trip"] = power.trip;
	ESP["service"] = power.serviceMode;
	ESP["bus_volts"] = power.bus_volts;
	ESP["bus_mA"] = power.bus_mA;
	ESP["ADreading"] = power.ADresult;
	String out;
	root.printTo(out);
	//webSocket.broadcastTXT(out);
}


//2020-03-30 moved from DCCcore
//2020-06-03 little point adding ESP augmentations if we don't intend to build a web-throttle
//and some of the system set-up features are now handled through type=dccUI

void nsJsonThrottle::_setJSONpower(String state, String setCurrent, String report) {
	//JMRI spec 2 is power on, 4 is off

	if (state == "2") {
		/*enable track power. note that the nodeMCU has an LED that is active low
		*LMD18000 needs a logic high into pwm*/
		digitalWrite(PIN_POWER, POWER_ON);
		power.trackPower = true;
		power.trip = false;
	}
	if (state == "4") {
		/*disable track power*/
		digitalWrite(PIN_POWER, POWER_OFF);
		power.trackPower = false;
		power.trip = false;
	}

	/*Digitrax states are 4 is off, 2 is active*/

	int16_t c = setCurrent.toInt();
	//if (c != 0) { power.currentLimit = c; }

	/*enable/dis reporting of power status*/
	if (report == "true") { power.report = true; }
	if (report == "false") { power.report = false; }
}



//2020-5-26 if loco[].consistID!=0 then we cannot grab the loco as it is in use, instead we are in dual cab mode
//there if 4 locos are allocated and all in a wiT then we cannot operate the desired loco address
//also need to call a replication routine

/*set loco from json string.  we can support 14 bit addresses
returns the index of loco[] that address maps to. returns -1 if no slot can be matched/allocated*/
int8_t nsJsonThrottle::_setJSONthrottle(String address, String speed, String direction, String func[], String throttle, String step, String maxStep, String useLong) {
	uint8_t i;
	if (address == "0") { return -1; }
	if ((address == "") && (throttle == "")) { return -1; }
#ifdef TRACE
		Serial.println("setJSONthrottle");
#endif
	/*we can match on numeric address or fail that, throttle name.  BUT code below will match on numeric address or assign a new slot, therefore
	we a separate test for throttle val and address="", and do a search*/
	/*search for matching loco address or assign a slot. this is the only elegant use of goto in programming ;-)
	 *search first for the matching address slot, then an empty zero-address slot and finally a zero speed slot
	 *where another loco is at rest allowing us to take its slot*/
		bool blUseLong= useLong == "true" ? true : false;
		
		for (i = 0;i < MAX_LOCO;i++) {
		if ((loco[i].address == address.toInt()) && (loco[i].useLongAddress==blUseLong))  { goto found_i; }
		//if (strcmp((char*)loco[i].throttle,buff)==0){goto found_i;}
		if (throttle.compareTo(loco[i].name) == 0) { goto found_i; }
	}
		/*else pick up an empty slot*/
	for (i = 0;i < MAX_LOCO;i++) {
		if (loco[i].address == 0) { goto found_i; }
	}
	/*else use a slot where loco is at rest, AND loco is not under a WiThrottle control */
	for (i = 0;i < MAX_LOCO;i++) {
		if (loco[i].speed == 0 && loco[i].consistID == 0) { goto found_i; }
	}
#ifdef TRACE
		Serial.println("\n _setJSONthrottle no available slots");
#endif
	return -1;

found_i:
	Serial.printf("\n _setJSONthrottle slot%d\n",i);

	/*found a slot, write values if they exist, set functionflag on the assumption that we are polling
	just for a slot-check and will need all F-values returned.*/
	if (maxStep == "128") { loco[i].use128 = true; }
	if (maxStep == "28") { loco[i].use128 = false; }
	if (useLong == "true") { loco[i].useLongAddress = true; }
	if (useLong == "false") { loco[i].useLongAddress = false; }
	/*consistency check, performed on every call*/
	if (address.toInt() > 127) { loco[i].useLongAddress = true; }


	//if (nudge!=""){loco[i].nudge=nudge.toInt();}

	/*set throttle name if present https://stackoverflow.com/questions/17158890/transform-char-array-into-string
	https://stackoverflow.com/questions/17158890/transform-char-array-into-string */
	if (throttle != "") {
		throttle.toCharArray(loco[i].name, 8);
#ifdef TRACE
		Serial.println((char*)loco[i].name);
#endif
	}

	/*address could be zero if throttle value only was supplied*/
	if (address.toInt() != 0) { loco[i].address = address.toInt(); }
	/*use speed step, if supplied, as a priority over percentile speed value*/
	if (step == "") {

		if (speed != "") {
			loco[i].speed = speed.toFloat();
			if (loco[i].speed >= 0) {
				if (loco[i].use128) {
					loco[i].speedStep = int(0.05 + (loco[i].speed * 126));
				}
				else {
					loco[i].speedStep = int(0.05 + (loco[i].speed * 28));
				}
			}
			else
			{/*negative speed is eStop on that loco. The exact code to send is calculated in DCCpacket*/
				loco[i].speed = 0;
				loco[i].eStopTimer = LOCO_ESTOP_TIMEOUT;
				loco[i].speedStep = 0;
			}
			loco[i].changeFlag = true;
		}

	}
	else {
		/*we were sent a speed step value*/
		if (step.toInt() >= 0) {

			loco[i].speedStep = step.toInt();
			Serial.println(loco[i].speedStep, DEC);
			/*calc float speed value */
			if (loco[i].use128) {
				loco[i].speed = float(loco[i].speedStep / 128.0);
			}
			else {
				loco[i].speed = float(loco[i].speedStep / 28.0);
			}

		}
		else {
			/*neg speed is eStoop*/
			loco[i].speed = 0;
			loco[i].eStopTimer = LOCO_ESTOP_TIMEOUT;
			loco[i].speedStep = 0;
		}
		loco[i].changeFlag = true;
	}

	/*casting to bool does not work, nor does x.toUpperCase()*/
	/*true is a 1 and false a 0 in arduino. Remember direction="" if not supplied*/
	if (direction != "") {
		loco[i].changeFlag = true;
		loco[i].forward = direction == "true" ? true : false;
	}
		


	/*process the array of F values, 13 bits as NRMA only define F0-F12. see S-9.2.1 para 275.*/
	loco[i].functionFlag = false;
	uint16_t f = loco[i].function;
	for (uint8_t j = 0;j < 13;j++) {
		if (func[j] == "true") { bitSet(f, j); }
		if (func[j] == "false") { bitClear(f, j); }
	}
	if (loco[i].function != f) {
		loco[i].function = f;
		loco[i].functionFlag = true;
	}

	/*2020-05-27 before we return replicate these changes across any WiThrottle consist this loco is part of*/
	replicateAcrossConsist(i);

	return i;

/*note, wiThrottle has the ability to broadcast loco params separately to function params, websockets has to send
an entire loco bundle if ether a loco or function param is changed*/

}

/*JSON RELATED ROUTINES*/

/*set turnout from json string, accepts 11 bit addresses
 *returns the accessory slot impacted
 *returns -2 if no change
 *note only turnouts with 2 digit addresses can be displayed in the local
 *UI, so we don't flag those above 99 for rebroadcast
 *we can action turnouts in the address range 1-2041
 *
 *{"type":"turnout","data":{"name":"IT99","state":4},"ESP":{"address":22}}
 *returns with index of turnout slot modified*/
int8_t nsJsonThrottle::_setJSONturnout(String address, String name, String state) {
	if (state == "") { return -2; }
	if ((address == "") && (name == "")) { return -2; }
	uint8_t i;

	/*toInt fails with a zero return*/
	uint16_t a = address.toInt();
	/*try using the JSON name instead*/
	if (a == 0) { a = name.toInt(); }

	if (a == 0) {
		/*if no address or numeric name provided, try searching on char name, however we need to confirm this has
		 *a numeric address value already associated with it*/
		if (name == "") { return -2; }

		for (i = 0;i < MAX_TURNOUT;i++) {
			/*compare string to the char array .name.  */
			if (name.compareTo(turnout[i].name) == 0) {
				a = turnout[i].address;
#ifdef TRACE
				Serial.print("setJSONacc strComp ");  //debug
				Serial.println(a, DEC);  //DEBUG
#endif
				break;
			}
		}
	}

	/*if a is still zero, fail*/
	if (a == 0) { return -2; }

	/*if out of range, fail*/
	if (a > 2048) { return -2; }

#ifdef TRACE
	Serial.println("setJSONAccessory state=");
	Serial.println(state);
#endif


	/*state is transferred to the accessory object which drives the DCC packet engine*/
	/*2020-4-29 block removed.  instead we use changeFlag to trigger this*/

	/*now update UI history, we only do this for addresses <100
	 *code below copied from setTurnoutFromKey()
	 */
	if (a > 99) { return -2; }
#ifdef TRACE
	Serial.println("check history");
#endif // TRACE

	/*find the age of oldest history, and capture the oldest slot.  clear any selected flag*/
	uint8_t age;
	age = 0;
	uint8_t oldestSlot;
	oldestSlot = 0;

	for (i = 0;i < MAX_TURNOUT;i++) {
		if (turnout[i].history > age) { age = turnout[i].history;oldestSlot = i; }
		turnout[i].selected = false;
	}

	/*scan addresses, is there a match to an existing slot?*/
	for (i = 0;i < MAX_TURNOUT;i++) {
		if (turnout[i].address == a)
		{
			turnout[i].selected = true;
			break;
		}
	}

	/*if we fail to find an existing slot, assign one based on history*/
	if (i == MAX_TURNOUT) {
		turnout[oldestSlot].address = a;
		turnout[oldestSlot].selected = true;
		turnout[oldestSlot].history = 0;
	}

	/*at this point we always have a slot selected, increment history of all other items*/
	uint8_t r = -2;

	for (i = 0;i < MAX_TURNOUT;i++) {
		if (!turnout[i].selected)
		{
			turnout[i].history++;
		}
		else
		{
			turnout[i].thrown = state == "4" ? true : false;
			turnout[i].changeFlag = true;
			r = i;
			/*set name*/
			if (name != "") { name.toCharArray(turnout[i].name, 8); }
		}
	}

	debugTurnoutArray();  //DEBUG
	return (r);
}

void nsJsonThrottle::_setJSONcv(String cv, String val, String mode) {
	if ((cv == "") || (val == "") || (mode == "")) { return; }

	/*possibly should send Estop before entering service mode*/

	m_cv.cvReg = cv.toInt();
	if (m_cv.cvReg > 1024) { m_cv.cvReg = 1024; }
	m_cv.cvData = val.toInt();

	//write actions
	if (mode == "direct") {
		dccSE = DCC_SERVICE;
		m_cv.timeout = 8;  //2 sec
		m_cv.state = D_START;
	}
	else
	{//pg-reg mode
		dccSE = DCC_SERVICE;
		m_cv.timeout = 8;  //2 sec
		m_cv.state = PG_START;
	}
#ifdef TRACE
	Serial.println("exit setJSONcv");
#endif
}

//Note on arduino String class. these can lead to memory leaks.  e.g. if you pass a String to a function it gets copied
//and so is less memory efficient than passing a pointer to a char array
//that said, we'd need to convert all the json incoming strings to char arrays...
//const char *v = root["address"];  i think this could work.  e.g. myfunc(String s) would take root["address"] but 
//one can also capture it as *v and then call a function.  NOPE does not work.



/* when digiTracks first connects, it sends these requests
{"type":"list", "list" : "turnouts"}
{"type":"list", "list" : "roster"}
{"type":"list", "list" : "lights"}
{"type":"list", "list" : "sensors"}
{"type":"list", "list" : "routes"}


*/

void nsJsonThrottle::_broadcastJSONroster(String t) {
	/*send one of the roster lists.  Cannot implement until I can find documentation on them
	will broadcast to all clients*/


}

/*call from main loop, will look for change flags and broadcast throttle or turnouts*/
void nsJsonThrottle::broadcastJSONchanges(bool clearFlags) {
	uint8_t i;
	for ( i= 0;i < MAX_LOCO; i++) {
		/*for a given slot, we only broadcast slot once, even though it might be loco or function param changes*/
		if (loco[i].changeFlag) {
			_broadcastJsonThrottle(i);
			continue;
		}
		if (loco[i].functionFlag) {
			_broadcastJsonThrottle(i);
			continue;
		}
	}

	if ((i<MAX_LOCO) && clearFlags) {
		/*clear both loco and function flags for specific loco above*/
		loco[i].changeFlag = false;
		loco[i].directionFlag = false;
		loco[i].functionFlag = false;
	}

	/*now handle turnouts*/

	for (uint8_t i = 0;i < MAX_TURNOUT; i++) {
		if (turnout[i].changeFlag) {

#ifdef TRACE
			Serial.println("broadcastJsonChanges for turnout");
#endif
			_broadcastJsonTurnout(i);

			/*queue change with the packet engine*/
			accessory.thrown = turnout[i].thrown;
			accessory.address = turnout[i].address;

			/*execute now by redirecting the DCC state engine. We can execute addresses up to 2048
			note: the DCCcore state engine responds to DCC_ACCESSORY and processes the accessory value
			before moving state to DCC_LOCO.  accessory value remains intact*/
			dccSE = DCC_ACCESSORY;

			if (clearFlags) { turnout[i].changeFlag = false; }
			break;
		}
	}

}


/*used for controller-to-webpage communication*/
/*
void nsJsonThrottle::DCCwebSendWS(char* msg) {
	webSocket.broadcastTXT(msg, sizeof(msg));
}

void nsJsonThrottle::DCCwebSendWS(JsonObject& out) {
	String sOut;
	out.prettyPrintTo(sOut);
	//https://arduinojson.org/v5/api/jsonobject/prettyprintto/
	webSocket.broadcastTXT(sOut);
}
*/