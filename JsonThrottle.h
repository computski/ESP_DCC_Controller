// JsonThrottle.h

#ifndef _JSONTHROTTLE_h
#define _JSONTHROTTLE_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include "Global.h"
//#include <WebSocketsClient.h>
//#include <WebSockets.h>
//#include <WebSocketsServer.h>

#include "DCCweb.h"

/*2020-03-30 support for JRMI JSON throttle protocol running over a websocket*/

/*think about overlap between ESP-web-json-UI and jmri-json.  e.g. how does jmri-json send a cv change?*/

namespace nsJsonThrottle {

	void broadcastJsonPower(void);
	void broadcastJSONchanges(bool clearFlags);
	void sendWSmessage(char* msg);
	void processJson(JsonObject& root);



	/*declare as static, limiting scope to this module*/
	static int8_t _setJSONthrottle(String address, String speed, String direction, String func[], String throttle, String step, String maxStep, String useLong);
	static int8_t _setJSONturnout(String address, String name, String state);
	static void _setJSONpower(String state, String setCurrent, String report);
	static void _setJSONcv(String cv, String val, String mode);
	static void _broadcastJSONroster(String t);

	static void _broadcastJsonThrottle(int k);
	static void _broadcastJsonTurnout(int8_t k);


}

#endif

