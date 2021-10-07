// 
// 
// 

#include "DCCweb.h"
#include "DCCcore.h"
#include "WiThrottle.h"

/*
2020-12-15 this module uses ArudinoJson library 5x which differs significantly from 6x

2020-11-30 this module handles all HTTP and Websocket connectivity.  HTTP is used to serve a static web page 
and then websockets are used for interaction on that page.  The websocket here is also used to support 
the JSON Throttle aka DigiTrains, if used.


see https://github.com/bblanchon/ArduinoJson
*/

//bear in mind enabling trace will enable serial dumps, these take time and may delay subsequent code execution

using namespace nsDCCweb;

//reate a web server on port 80.  problems https://github.com/esp8266/Arduino/issues/4085
ESP8266WebServer web(80);

//2021-01-29 declare as a pointer, we need to instantate once wsPort is pulled from eeprom
WebSocketsServer *webSocket;


#pragma region WEBSERVER_routines

void handleRoot() {
	web.send(200, "text/html", "<h1>You are connected</h1>");
	trace(Serial.println(F("HTTP server handleRoot."));)
}

String getContentType(String filename) { // convert the file extension to the MIME type
	if (filename.endsWith(".htm")) return "text/html";
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".js")) return "application/javascript";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
	trace(Serial.println("handleFileRead: " + path);)
	if (path.endsWith("/")) path += "index.htm";         // If a folder is requested, send the index file
	String contentType = getContentType(path);            // Get the MIME type
	if (SPIFFS.exists(path)) {                            // If the file exists
		File file = SPIFFS.open(path, "r");                 // Open it
		size_t sent = web.streamFile(file, contentType); // And send it to the client
		file.close();                                       // Then close the file again
		return true;
	}
	trace(Serial.println("\tFile Not Found " + path);)
	return false;                                         // If the file doesn't exist, return false

	}

//render a minimal hardware object as json back to the GET request, this gives the client the wsPort
void getHardware() {
	StaticJsonBuffer<600> jsonBuffer;
	JsonObject& out = jsonBuffer.createObject();
	out["type"] = "dccUI";
	out["cmd"] = "hardware";
	out["SSID"] = bootController.SSID;
	out["pwd"] = bootController.pwd;
	out["version"] = bootController.softwareVersion;
	out["wsPort"] = bootController.wsPort;
	out["IP"] = bootController.IP;
	out["action"] = "poll";
	out["STA_SSID"] = bootController.STA_SSID;
	out["STA_pwd"] = bootController.STA_pwd[0] == '\0' ? "none" : "*****";
	//additional debug params for this routine only
	out["uptime"] = int(millis() / 1000);
	out["clients"] = nsWiThrottle::clientCount();
	out["volt"] = getVolt();
	out["quiescent"] = power.quiescent_mA;
	out["busmA"] = power.bus_mA;
	out["base"] = power.ackBase_mA;
	out["AD"] = power.ADresult;
	out["heap"] = ESP.getFreeHeap();

	trace(out.printTo(Serial);) 


//We can avoid a String class, but need to guesstimate a useful buffer size
	char jsonChar[512];
	//out.printTo((char*)jsonChar, out.measureLength() + 1);
	out.prettyPrintTo(jsonChar, sizeof(jsonChar)); //if you want to be more explicit to avoid buffer overun
	web.send(200, "text/json", jsonChar);
	if (sizeof(jsonChar) < out.measurePrettyLength()) {
		Serial.printf("BU1 %d\n\r", out.measurePrettyLength());
	}

}

//render loco roster as json back to the GET request
void getRoster() {
	DynamicJsonBuffer jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();
	root["type"] = "dccUI";
	root["cmd"] = "roster";
	JsonArray& slots = root.createNestedArray("locos");
	
	int i = 0;
	for (auto loc : loco) {
		JsonObject& s = jsonBuffer.createObject();
		s["slot"] = i++;
		s["addr"] = loc.address;
		s["useLong"] = loc.useLongAddress;
		s["use128"] = loc.use128;
		slots.add(s);
		Serial.printf("slot %d \r\n", i);
	}

	root.prettyPrintTo(Serial);

	String r;
	root.prettyPrintTo(r);
	web.send(200, "text/json", r);
}



void nsDCCweb::startWebServices() { // Start a WebSocket server
	web.on("/", handleRoot);
	web.onNotFound([]() {                              // If the client requests any URI
		if (!handleFileRead(web.uri()))                  // send it if it exists
			web.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
	});

	//special GET handlers
	//https://forum.arduino.cc/index.php?topic=476291.0
	web.on("/hardware", HTTP_GET, []() {getHardware();});
	web.on("/roster", HTTP_GET, []() {getRoster();});
	
	web.begin();    // start the HTTP server
	Serial.println(F("HTTP server started."));

	SPIFFS.begin();                           // Start the SPI Flash Files System
	webSocket = new WebSocketsServer(bootController.wsPort);
	webSocket->begin();                         // start the websocket server
	webSocket->onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'

	Serial.printf("WebSocket start port %d\n", bootController.wsPort);

}

//call regularly from main loop
void nsDCCweb::loopWebServices(void) {
	web.handleClient();
	webSocket->loop();
}

#pragma endregion


#pragma region WEBSOCKET_routines

void nsDCCweb::webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) { // When a WebSocket message is received

	switch (type) {
	case WStype_DISCONNECTED:             // if the websocket is disconnected
		trace(Serial.printf("[%u] Disconnected!\n", num);)
		break;
	case WStype_CONNECTED: {              // if a new websocket connection is established
		IPAddress ip = webSocket->remoteIP(num);
		trace(Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);)
		}
		break;
	case WStype_TEXT:                     // if new text data is received
	  //Serial.printf("[%u] get Text: %s\n", num, payload);
		trace(Serial.printf("\nfrom WS: %s\n", payload);)
		/*deserialise this. Need 6 value pairs or more, use a dynamic buffer*/
		DynamicJsonBuffer jsonBuffer;
		JsonObject& root = jsonBuffer.parseObject(payload);
		//CAUTION: parseObject will modify payload
		

		if (!root.success()) {
			trace(Serial.println(F("parseObject() failed"));)
			return;
		}

		const char *sType= root["type"];
		if (sType == nullptr) return; 
		
		if (strcmp(sType,"dccUI")==0) {
			//callout to DCCweb module
			nsDCCweb::DCCwebWS(root);
			return;
		}

		/*for all other types, call out to the JsonThrottle if module loaded*/
#ifdef _JSONTHROTTLE_h
		nsJsonThrottle::processJson(root);
#endif
	}//end switch
}//end websocket event

void nsDCCweb::sendJson(JsonObject& out) {
	//char* jsonChar;   //this will not work, its a pointer to an as yet undefined buffer
	//out.printTo((char*)jsonChar, out.measureLength() + 1);

	
	//We can avoid a String class, but need to guesstimate a useful buffer size
	//2021-10-07 payload increased to 800 bytes to support max loco=8
	char payload[800];
	out.printTo(payload, sizeof(payload)); //if you want to be more explicit to avoid buffer overun
	webSocket->broadcastTXT(payload);
	if (sizeof(payload) < out.measureLength()) {
		Serial.printf("BU2 %d\n\r", out.measureLength());
	}

}

//process a JSON message
void nsDCCweb::DCCwebWS(JsonObject& root) {

		const char *cmd = root["cmd"];
		if (cmd == nullptr) return;

		if (strcmp(cmd,"power") == 0) {
			//if a param is blank, means client web page wants to poll value
			//if client provides a value for mA_limit or V_limit, write this to EEPROM
			//to avoid unintential writes, client should send 0 values
			const char* v = root["mA_limit"];
			if (v != nullptr) {
				if (atoi(v) > 0 && atoi(v) != bootController.currentLimit) {
					bootController.isDirty = true;
					bootController.currentLimit = atoi(v);
					if (bootController.currentLimit > 4000) bootController.currentLimit = 4000;
					if (bootController.currentLimit < 250) bootController.currentLimit = 250;

				}
			}

			v = root["V_limit"];
			if (v != nullptr) {
				if (atoi(v) > 0 && atoi(v) != bootController.voltageLimit) {
					bootController.isDirty = true;
					bootController.voltageLimit = atoi(v);
				}
			}
		
			v = root["track"];
			if (v != nullptr) {
				setPower(cBool(v));
			}

			v = root["SM"];
			if (v != nullptr) {
				//additionally set the power.serviceMode flag if present
				//used for service mode programming
				power.serviceMode = cBool(v);
			}
								   
			dccPutSettings();
			DynamicJsonBuffer jsonBuffer;
			JsonObject& out = jsonBuffer.createObject();
			
			out["type"] = "dccUI";
			out["cmd"] = "power";
			out["mA"] = (int)power.bus_mA;
			out["V"] = power.bus_volts;
			out["trip"] = power.trip;
			out["track"] = power.trackPower;
			out["mA_limit"] = bootController.currentLimit;
			out["V_limit"] = bootController.voltageLimit;
			out["SM"] = power.serviceMode;

			trace(out.printTo(Serial);)
			sendJson(out);

		}

		if (strcmp(cmd,"hardware") == 0) {
			//{"type":"dccUI", "cmd":"hardware","SSID" : "DDC_01", "Password" : "none", "IP" : "192.168.4.1","version":20201201,"action":"poll" ,"wsPort":10,"wiPort":20}
			//{ "type": "dccUI", "cmd": "hardware", "SSID": "DCC_02", "IP": "192.168.7.1", "MAC":"return of the", "pwd": "", "wsPort": 12080, "wiPort": 12090, "action": "poll" };
      

			const char *v = root["action"];
			bool restart = false;

			if ((v != nullptr) && strcmp(v, "write") == 0) {
				//user wishes to set one or more values

				v = root["SSID"];
				if (v != nullptr) {
					strncpy(bootController.SSID, v, sizeof(bootController.SSID));
					//cannot set the AP SSID to null
					if (bootController.SSID[0] == '\0') {
						strncpy(bootController.SSID, "DCC_ESP\0", sizeof(bootController.SSID));
					}

					restart = true;
				}

				v = root["STA_SSID"];
				if (v != nullptr) {
					strncpy(bootController.STA_SSID, v, sizeof(bootController.STA_SSID));
					//writing null is ok.
					restart = true;
				}

				v = root["pwd"];
				if (v != nullptr) {
					//"none" is used explicity to instruct system to set a null pwd
					if (strcmp(v, "none") == 0) { memset(bootController.pwd, '\0', sizeof(bootController.pwd)); }
					else {
						//length of password must be 8+ chars
						int i = 0;
						while (v[i] != '\0') { 
							i++;
							if (i == 7) break;
						}
						
						//only save passwords 8+ char
						if (i>=7) strncpy(bootController.pwd, v, sizeof(bootController.pwd));
					}
					restart = true;
				}

				v = root["STA_pwd"];
				if (v != nullptr) {
					//"none" is used explicity to instruct system to set a null pwd
					if (strcmp(v, "none") == 0) { memset(bootController.STA_pwd, '\0', sizeof(bootController.STA_pwd)); }
					else {
						//length of password must be 8+ chars
						int i = 0;
						while (v[i] != '\0') {
							i++;
							if (i == 7) break;
						}

						//only save passwords 8+ char
						if (i >= 7) strncpy(bootController.STA_pwd, v, sizeof(bootController.STA_pwd));
					}
					restart = true;
				}

				v = root["IP"];
				if (v != nullptr) {
					//ip address is stored as dot separated
					strncpy(bootController.IP, v, sizeof(bootController.IP));
					restart = true;
				}

				v = root["wsPort"];
				if (v != nullptr) {
					if (atoi(v) > 0 && atoi(v) <=65535) {
						restart = true;
						bootController.wsPort = atoi(v);
					}
				}

				v = root["tcpPort"];
				if (v != nullptr) {
					if (atoi(v) > 0 && atoi(v) <= 65535) {
						restart = true;
						bootController.tcpPort= atoi(v);
					}
				}

			}


			DynamicJsonBuffer jsonBuffer;
			JsonObject& out = jsonBuffer.createObject();
						
			out["type"] = "dccUI";
			out["cmd"] = "hardware";
			out["SSID"] = bootController.SSID;
			out["pwd"] = bootController.pwd[0] == '\0' ? "none":"*****";
			out["STA_SSID"] = bootController.STA_SSID;
			out["STA_pwd"] = bootController.STA_pwd[0] == '\0' ? "none" : "*****";	
			out["version"] = bootController.softwareVersion;
			out["wsPort"] = bootController.wsPort;
			out["wiPort"] = bootController.tcpPort;
			out["IP"] = bootController.IP;
			byte mac[6];
			WiFi.macAddress(mac);
			char buff[30];
			sprintf(buff, "%02X:%02X:%02X:%02X:%02X:%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
			out["MAC"] = buff;
			out["uptime"] = int(millis() / 1000);
			out["clients"] = nsWiThrottle::clientCount();
			
		
			if (restart) {
				//we successfully wrote a new value and a restart is required
				out["action"] = "success";
				out.printTo(Serial);
				bootController.isDirty = true;
				dccPutSettings();
				sendJson(out);  //should complete via ints
				delay(2000);
				Serial.println(F("ESP restart required"));
				//ESP.restart();

				//this may not reliably restart the unit, someone on internet says...
				//WiFi.forceSleepBegin(); wdt_reset(); ESP.restart(); while (1)wdt_reset();
				//but there may be a bug - restart may not reliably work after an EEPROM write

				//2020-12-14 a manual hard reset is required.  in the meantime, the server will display
				//the values it just wrote to eeprom.


			}

			//did not change anything, no restart required
			out["action"] = "poll";
			trace(out.printTo(Serial);)
			sendJson(out);
		}

		if (strcmp(cmd, "roster") == 0) {
		//incoming loco roster-change messages contain an array.  if we find a value has changed, we act on it
		//	var roster = {
		//  "type": "dccUI", "cmd" : "roster", "locos" : [{ "slot": 0, "address" : 3, "useLong" : false, "use128" : true, "name" : "", "inUse" : false },
		//  { "slot": 1, "address" : 4, "useLong" : false, "use128" : true, "name" : "ivor","inUse" : true }]
		//	}



			JsonVariant variant = root["locos"];
			if (variant.is<JsonArray>()) 
			{
				JsonArray& array = variant;
				
				/*test block
				Serial.println(array.size());
				for (int i = 0;i < MAX_LOCO;++i) {
					if (i == array.size()) break;
					if (!changeToSlot(i, array[i]["address"], cBool(array[i]["useLong"]), cBool(array[i]["use128"]), array[i]["name"])) continue;
					Serial.printf("change on %d\r\n", i);
					
				}
				*/

				
				for (int i = 0;i < MAX_LOCO;++i) {  
					//client may have sent only a couple of slots
					if (i == array.size()) break;

					//ignore any unchanged roster entries
					if (!changeToSlot(i, array[i]["address"], cBool(array[i]["useLong"]), cBool(array[i]["use128"]), array[i]["name"])) continue;
					

					{	//changes were made
						Serial.printf("change on %d\r\n", i);
						//to prevent runaway locos, you cannot modify a slot if loco is moving or is under control of a WiThrottle.
						if ((loco[i].speed > 0) || (loco[i].consistID != 0)) continue;

						int addr = atoi(array[i]["address"]);
						if (addr == 0) {
							//2021-10-02 don't allow the one remaining slot to be deleted. we must always have at least
							//one loco in the DSKY.  Also what if the DSKY was pointed at slot 2 and that is now deleted?
							//if it defaults to slot 0, will that be available?  maybe we just never allow deletion of slot zero
							int8_t activeSlots = 0;
							for (auto loc : loco) {
								if (loc.address != 0) activeSlots++;
							}
							//exit if this is the last active slot
							if (activeSlots == 1) continue;
							//otherwise clear the slot
							loco[i].address = 0;
							loco[i].forward = true;
							loco[i].use128 = false;
							memset(loco[i].name, '\0', sizeof(loco[i].name));
							loco[i].consistID = 0;
							loco[i].speed = 0;
							continue;
						}

						//look for address with short/long flag.  expect to find it in self-slot
						//if it exists in another slot then ignore it as we don't wish to create a dupe
						//else write it
						int j = 0;
						for (auto loc : loco) {
							if ((loc.useLongAddress == cBool(array[i]["useLong"])) && (loc.address==addr)) break;
							++j;
						}
						//we exit either with j pointing at the address in a slot, or with j==MAX_LOCOS indicating no match
						
						//bail if loco exists in another slot
						if ((j != MAX_LOCO) && (j != i)) continue;

						//at this point addr was unchanged (j==i) or it does not exist in any slot (j==MAX_LOCO) 
						//For new and unchanged, write back all params to loco[i];
										
						//validate the address
						if (addr < 1) continue;
						if (addr > 10239) continue;

						//proceed
						loco[i].forward = true;
						loco[i].speed = 0;
						loco[i].consistID = 0;
						loco[i].address = addr;
						loco[i].useLongAddress = cBool(array[i]["useLong"]);
						if (addr > 127) loco[i].useLongAddress = true;
						loco[i].use128 = cBool(array[i]["use128"]);
						memset(loco[i].name, '\0', sizeof(loco[i].name));
						strncpy(loco[i].name, array[i]["name"], sizeof(loco[i].name));
						bootController.isDirty = true; 
						trace(Serial.printf("slot %d updated\n\r", i);)
					}				
				}
				if (bootController.isDirty) bootController.flagLocoRoster = true;
				dccPutSettings();
			}
			
			//done with changes, now generate an output
			DynamicJsonBuffer jsonBuffer;
			JsonObject& out = jsonBuffer.createObject();
			out["type"] = "dccUI";
			out["cmd"] = "roster";
			JsonArray& slots = out.createNestedArray("locos");
			//https://arduinojson.org/v5/api/jsonobject/createnestedarray/
			int i = 0;
			for (auto loc : loco) {
				JsonObject &s = jsonBuffer.createObject();
				s["slot"] = i++;
				s["address"] = loc.address;
				s["useLong"] = loc.useLongAddress;
				s["use128"] = loc.use128;
				//slot is in use if speed is >0 or a WiThrottle has taken it
				s["inUse"] = loc.speed>0 ? true:(loc.consistID != 0);
				s["name"] = loc.name;
				slots.add(s);
			}

			trace(out.prettyPrintTo(Serial);)	
			sendJson(out);

#ifdef _WITHROTTLE_h
			nsWiThrottle::broadcastLocoRoster(nullptr);
#endif

		}



		//turnout roster
		if (strcmp(cmd, "turnout") == 0) {
			//incoming turnout roster-change messages contain an array.  if we find a value has changed, we act on it
			//  {"type": "dccUI", "cmd": "turnout", "turnouts": [{ "slot": 0, "address": 3, "state": "closed", "name":"siding 1"},
			//{ "slot": 1, "address" : 4, "state" : "thrown", "name" : "ivor"}] }
			
			JsonArray& array = root["turnouts"];;
			if (array.success()) {

				/*test block
				for (int i = 0;i < MAX_TURNOUT;++i) {
					if (i == array.size()) break;
					if (!changeToTurnout(i, array[i]["address"], array[i]["name"])) continue;
					Serial.printf("turnout change on %d\r\n", i);
				}
				*/

				//process any changes
				for (int i = 0;i < MAX_TURNOUT;++i) {
					if (i == array.size()) break;
					if (!changeToTurnout(i, array[i]["address"], array[i]["name"])) { 
						//2021-01-10 if turnout entry appears unchanged, check the state as user may have issued a command to toggle this
						bool newState = (strcmp(array[i]["state"], "thrown") == 0);

						if (turnout[i].thrown != newState) {
							turnout[i].thrown = newState;
							turnout[i].changeFlag = true;
						}
						//this function always sends the roster back to the webclient even if no changes were made
						//therefore the updated states will show
					continue; }
					trace(Serial.printf("turnout change on %d\r\n", i);)


					int addr = atoi(array[i]["address"]);
					if (addr == 0) {
						//clear the turnout slot
						turnout[i].address = 0;
						memset(turnout[i].name, '\0', sizeof(turnout[i].name));
						turnout[i].thrown = false;
						turnout[i].selected = false;
						continue;
					}

					//look for address. expect to find it in self-slot
					//if it exists in another slot then ignore it as we don't wish to create a dupe
					//else write it
					int j = 0;
					for (auto t : turnout) {
						if (t.address == addr) break;
						++j;
					}
					//we exit either with j pointing at the address in a slot, or with j==MAX_TURNOUTS indicating no match

					//bail if turnout exists in another slot
					if ((j != MAX_TURNOUT) && (j != i)) continue;

					//procceed, validate the address
					if (addr < 1) continue;
					if (addr > 1024) continue;

					//proceed
					turnout[i].address = addr;
					
					memset(turnout[i].name, '\0', sizeof(turnout[i].name));
					strncpy(turnout[i].name, array[i]["name"], sizeof(turnout[i].name));
					bootController.isDirty = true;
					trace(Serial.printf("turnout slot %d updated\n\r", i);)

				}
				dccPutSettings();
#ifdef _WITHROTTLE_h
				nsWiThrottle::broadcastTurnoutRoster(nullptr); 
#endif
			}
			

			//send the turnout roster
			DynamicJsonBuffer jsonBuffer;
			JsonObject& out = jsonBuffer.createObject();
			out["type"] = "dccUI";
			out["cmd"] = "turnout";
			JsonArray& slots = out.createNestedArray("turnouts");
			//https://arduinojson.org/v5/api/jsonobject/createnestedarray/
			int i = 0;
			for (auto t : turnout) {
				JsonObject &s = jsonBuffer.createObject();
				s["slot"] = i++;
				s["address"] = t.address;
				s["name"] = t.name;
				s["state"] = t.thrown ?"thrown":"closed";
				slots.add(s);
			}

			trace(out.prettyPrintTo(Serial);)
			sendJson(out);
		}//end turnout



		if (strcmp(cmd,"pom") == 0) {
			//note to change a long address, send CV17 then CV18. It appears most decoders won't change either until both
			//are received in sequence
			//pom = { "type": "dccUI", "cmd": "pom", "action": "byte", "addr":"S3", "cvReg": 0, "cvVal": "B23" };
			const char *action = root["action"];
			const char *addr = root["addr"];
			uint16_t cv_reg = root["cvReg"];
			const char *cv_val = root["cvVal"];
			

			if (action == nullptr) return;
			if (addr == nullptr) return;
			if (cv_val == nullptr) return;

			//the required action is evident in cv_val without need to look at root.action
			writePOMcommand(addr, cv_reg, cv_val);

			StaticJsonBuffer<200> jsonBuffer;
			JsonObject& out = jsonBuffer.createObject();
			out["type"] = "dccUI";
			out["cmd"] = "pom";
			out["action"] = "ok";
			sendJson(out);
		}



		if (strcmp(cmd,"service") == 0) {
			
			//service mode, but will only support byte writes and verifies
			//var service = { "type": "dccUI", "cmd" : "sevice", "reg" : 12, "value" : 13, "action" : "read" };
			//action is read|direct|exit|enter|result  direct=byte write in direct mode, and  is the only mode supported
			//exit is used to leave service mode and restore full power, is generated when you click on a link
			//entry to service mode will send eStop.
			
			const char *action = root["action"];
			uint16_t cv_reg = root["cvReg"];
			uint8_t cv_val = root["cvVal"];
			
			if (action == nullptr) return;
			
			if (strcmp(action, "enter") == 0) {
				//enter service mode, will send estop to all locos and set a 250mA trip threshold

				//causes exception 29
				//from WS: {"type":"dccUI","cmd":"service","reg":0,"value":-1,"action":"enter"}
				writeServiceCommand(0, 0, false, true, false);
			}
			
			//if you fail to exit SM, service.mode wll be true which limits power
			//to confirm, if you invoke dccSE=service i think it stays in this mode (service idle)
			//until you chose to exit
			if (strcmp(action, "exit") == 0) {
				//exit service mode, returns to full power
				Serial.println("svc#4");
				writeServiceCommand(0, 0, false, false, true);
			}
			
			if (strcmp(action, "direct") == 0) {
				//direct write
				if (cv_val == 0) return;
				writeServiceCommand(cv_reg, cv_val, false, false, false);
			}

			if (strcmp(action, "read") == 0) {
				//call once to iniate a read. the result is provided via a callback
				if (cv_reg == 0) return;
				writeServiceCommand(cv_reg, 0, true, false, false);
			}
			return;
		}


}


//returns true if one or more the params is different to that in the nominated slot
bool nsDCCweb::changeToSlot(uint8_t slot, const char *addr, bool useLong, bool use128, const char *name) {
	
	if (atoi(addr) != loco[slot].address) return true;
	if (loco[slot].useLongAddress != useLong)  return true;
	if (loco[slot].use128 != use128)  return true;

	//both name might be null
	if (((name == nullptr) && (loco[slot].name == nullptr))) return true;
	//or one of them...
	if ((name == nullptr) || (loco[slot].name == nullptr)) return true;
	//if we have valid name values to compare...
	if (strncmp(loco[slot].name, name,sizeof(loco[slot].name)) != 0) return true;
	
	return false;
	
}

//returns true if one or more the params is different to that in the nominated turnout slot
bool nsDCCweb::changeToTurnout(uint8_t slot, const char *addr, const char *name) {
	if (atoi(addr) != turnout[slot].address) return true;
	//both name might be null
	if (((name == nullptr) && (turnout[slot].name == nullptr))) return true;
	//or one of them...
	if ((name == nullptr) || (turnout[slot].name == nullptr)) return true;
	//if we have valid name values to compare...
	if (strncmp(turnout[slot].name, name, sizeof(turnout[slot].name)) != 0) return true;

	return false;

}

//transmit current power status over websocket
void nsDCCweb::broadcastPower(void) {
	StaticJsonBuffer<200> jsonBuffer;
	JsonObject& out = jsonBuffer.createObject();

	out["type"] = "dccUI";
	out["cmd"] = "power";
	out["mA"] = (int)power.bus_mA;
	out["V"] = power.bus_volts;
	out["trip"] = power.trip;
	out["track"] = power.trackPower;
	out["mA_limit"] = bootController.currentLimit;
	out["V_limit"] = bootController.voltageLimit;
	
	sendJson(out);
}

void nsDCCweb::setPower(bool powerOn) {
	if (powerOn) {
		/*turn on track power*/
		power.trip = false;
		power.trackPower = true;
		}
	else {
		/*turn off track power*/
		power.trackPower = false;
		power.trip = false;
	}

	{//block scope
		//using namespace nsWiThrottle;
		nsWiThrottle::broadcastPower();
		//do we need this or is the broadcast message regularly sent anyway?
	}
}

//send CV read result message
void nsDCCweb::broadcastReadResult(uint16_t cvReg, int16_t cvVal) {
	//this routine is a callback from DCCcore
	DynamicJsonBuffer jsonBuffer;
	JsonObject& out = jsonBuffer.createObject();

	out["type"] = "dccUI";
	out["cmd"] = "service";
	out["cvReg"] = cvReg;
	//the value will be -1 if read failed
	out["action"] = cvVal <0 ? "fail" : "result";
	out["cvVal"] = cvVal;
	trace(out.printTo(Serial);)
	sendJson(out);

}

//evaluate char array for 'true' keyword
bool nsDCCweb::cBool(const char *v) {
	if (v == nullptr) return false;
	return (strcmp(v, "true") == 0);
}

//broadcast any turnout changes that occurred outside of this module
void nsDCCweb::broadcastChanges(void) {
	

	//if the loco roster has changed, send it
	if (bootController.flagLocoRoster) {
		trace(Serial.println(F("nsDCCweb::broadcastChanges"));)
		DynamicJsonBuffer jsonBuffer;
		JsonObject& out = jsonBuffer.createObject();
		out["type"] = "dccUI";
		out["cmd"] = "roster";
		JsonArray& slots = out.createNestedArray("locos");
		//https://arduinojson.org/v5/api/jsonobject/createnestedarray/
		int i = 0;
		for (auto loc : loco) {
			JsonObject &s = jsonBuffer.createObject();
			s["slot"] = i++;
			s["address"] = loc.address;
			s["useLong"] = loc.useLongAddress;
			s["use128"] = loc.use128;
			//slot is in use if speed is >0 or a WiThrottle has taken it
			s["inUse"] = loc.speed > 0 ? true : (loc.consistID != 0);
			s["name"] = loc.name;
			slots.add(s);
		}
			sendJson(out);
	}


	//if the turnout roster has changed, send it
	int i;
	if (!bootController.flagTurnoutRoster) {
		//if there's a turnout state change, also send the roster
		for (i = 0;i < MAX_TURNOUT;i++) {
			if (turnout[i].changeFlag) break;
		}
		if (i >= MAX_TURNOUT) return;
	}
	

	//send the turnout roster
	DynamicJsonBuffer jsonBuffer;
	JsonObject& out = jsonBuffer.createObject();
	out["type"] = "dccUI";
	out["cmd"] = "turnout";
	JsonArray& slots = out.createNestedArray("turnouts");
	i = 0;
	for (auto t : turnout) {
		JsonObject &s = jsonBuffer.createObject();
		s["slot"] = i++;
		s["address"] = t.address;
		s["name"] = t.name;
		s["state"] = t.thrown ? "thrown" : "closed";
		slots.add(s);
	}
	sendJson(out);

}

#pragma endregion

