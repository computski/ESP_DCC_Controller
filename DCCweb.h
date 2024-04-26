// DCCweb.h

#ifndef _DCCWEB_h
#define _DCCWEB_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif

#include "Global.h"
#include <ArduinoJson.h>   //from arduino library manager.  you want VERSION 7 only!
#include <ESP8266WebServer.h>
#include <LittleFS.h>   
#include <WebSockets.h>  //from arduino library manager. Markus Sattler 
#include <WebSocketsServer.h>
#include <ESP8266WiFi.h>



namespace nsDCCweb {

	void startWebServices();
	void loopWebServices(void);
	void broadcastPower(void);
	void broadcastReadResult(uint16_t cvReg, int16_t cvVal);
	void broadcastChanges(void);

	static void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
	static void DCCwebWS(JsonDocument doc);
	static void sendJson(JsonObject& out);
	static void sendJson(JsonDocument out);
	static bool changeToTurnout(uint8_t slot, uint16_t addr, const char* name);
	static bool changeToTurnout(uint8_t slot, const char* addr, const char* name);
	static bool changeToSlot(uint8_t slot, uint16_t address, bool useLong, bool use128, const char* name);
	static bool changeToSlot(uint8_t slot, const char* addr, bool useLong, bool use128, const char* name);
	static void setPower(bool powerOn);
	static bool cBool(const char* v);
	}

#endif