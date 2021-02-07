// 
// 


/*
	Name:       ESP_DCC_Controller.ino
	Created:	2021-02-07
	Author:     Julian Ossowski
	Target:		NodeMCU 1.0 (ESP-12E Module)
	Note:		This device is 4M flash with 1M spiffs. The spiffs hold the webserver files
				and must be uploaded separately via vMicro publish server feature
	Serial:		default speed on the serial is 115200
	IDE:		vMicro inside MS Visual studio.  Should also compile in the Arduino IDE.

	Hardware note: the ESP is a 3v3 device, the LCD device is built for 5v but can run on 3v3 with a reduced backlight brightness
	and it will also need a -ve voltage bias for the LCD.  This can be provided via a charge pump driven off the DCC signal.
	OR it is possible to run the LCD, keypad and INA device off 5v, but it is necessary to diode-clamp the SDA line to 3v3.
	Incidentally the nodeMCU only has a 3v3 regulator on board.  Vin is connected to the USB power which is assumed to be 5v.
	It then feeds via a diode into the 3v3 regulator.  Its possible to feed Vin with 12v for example and the regulator will drop
	this to 3v3, however the regulator will run hot.  Also the USB socket will have 12v present on it, which is a risk for any
	device plugged into it.
*/



#include <ESP8266WebServerSecure.h>
#include <ESP8266WebServer.h>
#include "Global.h"
#include <EEPROM.h>
#include <Adafruit_INA219.h>
#include <ESP8266WiFi.h>

#include "DCCcore.h"
#include "DCClayer1.h"
#include "DCCweb.h"
#include "WiThrottle.h"

//#include "JsonThrottle.h"  exclude for now


//PIN ASSIGNMENTS - see global.h
//found that NMI in DCClayer1 conflicts with the WSP826WebServer. Had to switch to regular ints.  There is no conflict with Websockets



uint16_t secCount;
bool    bLED;


/*DCC signal will appear on gpio12, which is D6*/
uint32 io_info[3] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO12, 12 }; //primary on GPIO12 D6
uint32 io_infoA[3] = { PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO14, 14}; //primary on GPIO14 D5


void setup() {
  	Serial.begin(115200);
	Serial.println(F("\n\nBoot DCC ESP"));
	trace(Serial.println(F("trace enabled"));)
	/*drive one output and a second in anti-phase*/
	dcc_init(io_info, 0);
	dcc_init(io_infoA, 1);

	DCCcoreBoot();
	//restore settings from EEPROM
	dccGetSettings();


	pinMode(PIN_HEARTBEAT, INPUT_PULLUP); //D0 with led

	Serial.printf("Setting soft-AP %s pwd=%s\n\r", bootController.SSID, bootController.pwd);

	WiFi.persistent(false);
	WiFi.setAutoConnect(false);
	WiFi.setAutoReconnect(false);
	WiFi.mode(WIFI_AP);


	//Passwords need to be >8 char and start with an alpha char
	//failure to do so results in an open network
	WiFi.softAP(bootController.SSID, bootController.pwd);
	
	//wait for the softAP to start, then set the ip address
	delayMicroseconds(500);

	//IPAddress class requires the address to be provided as 4 octets
	uint8_t myIP[4];
	char *p = nullptr;
	char ipBoot[17];
	strcpy(ipBoot, bootController.IP);
	
	//strtok modifies its arguement, have to use a copy.
	p = strtok((char *)ipBoot, ",.");
	int i = 0;
	while (p != NULL) {
		myIP[i] = atoi(p);
		i++;
		if (i == 4) break;
		//more data?
		p = strtok(NULL, ",.");
	}
	
	IPAddress Ip(myIP[0], myIP[1], myIP[2], myIP[3]);
	IPAddress NMask(255, 255, 255, 0);
	WiFi.softAPConfig(Ip, Ip, NMask);
	//declare the IP of the AP
	Serial.println(WiFi.softAPIP());

	//now establish a connection to an external WiFi network, e.g. your home internet
	//if STA_SSID is blank, no connection is atttempted
	/*disable for now.
	if (bootController.STA_SSID[0] != '\0') {
		//
		WiFi.begin(bootController.STA_SSID, bootController.STA_pwd);
		Serial.print(F("Connecting to "));
		Serial.print(bootController.STA_SSID);
		// Loop continuously while WiFi is not connected
		int t = 0;
		while (WiFi.status() != WL_CONNECTED)
		{
			delay(100);
			Serial.print(".");
			if (++t >= 50) break;
		}

		if (WiFi.status() == WL_CONNECTED) {
			Serial.print(F("Connected! IP address: "));
			Serial.println(WiFi.localIP());
		}else{
			Serial.println(F("FAILED"));
		}

	}
	*/

	Serial.printf("mode %d\r\n", WiFi.getMode());

#ifdef _DCCWEB_h		
	nsDCCweb::startWebServices();
#endif
	nsJogWheel::jogInit();

	/*2020-04-02 start WiThrottle protocol*/
#ifdef _WITHROTTLE_h
	nsWiThrottle::startThrottle();
#endif


} //end boot




char debugS;
int8_t debugThing = 0;

void loop() {

#ifdef _DCCWEB_h
		nsDCCweb::loopWebServices();   // constantly check for websocket events

		/*2020-05-03 new approach to broadcasting changes over all channels.
		A change can occur on any comms channel, might be WiThrottle, Websockets or the local hardware UI
		Flags are set in the loco and turnout objects to denote the need to broadcast.
		These flags are cleared by the local UI updater as the last in sequence*/

		nsDCCweb::broadcastChanges();
#endif

#ifdef _JSONTHROTTLE_h
		nsJsonThrottle::broadcastJSONchanges(false);
#endif

#ifdef _WITHROTTLE_h
		nsWiThrottle::broadcastChanges(false);   
#endif

		//broadcast turnout changes to line and clear the flags
		updateLocalMachine();
		
		//call DCCdore once per loop. We no longer use the return value
		DCCcore();
		

		//serial debug
		if (Serial.available() > 0) {
			debugS = Serial.read();
			if (debugS == 'j') {
				Serial.println(F("debug triggered"));
				loco[0].debug = true;
			}
		}


	if (quarterSecFlag) {
		/*isQuarterSecFlag will return true and also clear the flag*/
		quarterSecFlag = false;

		//2021-01-29 call processTimeout every 250mS to give better resolution over timeouts
#ifdef _WITHROTTLE_h
		nsWiThrottle::processTimeout();
#endif
		
		secCount++;
		if (secCount >= 8) {
			/*toggle heartbeat LED, every 2s*/
			bLED = !bLED;
			secCount = 0;
			
			//send power status out to web sockets
#ifdef _DCCWEB_h
			nsDCCweb::broadcastPower();
#endif

#ifdef _JSONTHROTTLE_h
			/*transmit power status to websocket*/
			nsJsonThrottle::broadcastJsonPower();
#endif

#ifdef _WITHROTTLE_h
			/*transmit power status to WiThrottles*/
			//2020-11-28 no need to do this so frequently
			//nsWiThrottle::broadcastWiPower();
#endif
		}


	}//qtr sec

}






