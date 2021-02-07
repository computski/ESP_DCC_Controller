# ESP_DCC_Controller
DCC controller based on nodeMCU ESP12-E module for model railroad control.  This is an ESP8266 device, not an ESP32.

Implements a DCC controller and JRMI server which supports mobile clients running EngineDriver (android) or WiThrottle (IOS). To allow a lower cost build, it is not necessary to fit the optional 4 x 4 keypad matrix, Jogwheel and 1602 LCD display.

Generates a DCC signal as a low level routine.  This is fed with DCC packets from higher level routines which handle keyboard/display
and communications over TCP with the JRMI throttles (on mobile phones).

The system also supports a web interface giving a means to control the current and voltage safety trip levels, as well as parameters such
as SSID and IP address.  The web interface also supports editing of locomotive and turnout rosters - these can also be modified via the
keypad, but this may not be fitted by the builder if they opt for a mobile only version.

Similarly locomotive decoder programming is supported on the unit, both service mode and POM, but JRMI throttles don't support this, so 
it is also provided on a web interface.

The web interface consists of static HTML pages served through a webserver - these pages are held in the data directory and this must also be uploaded to the target device.  Interactivity on these pages is provied via Websockets and Javascript on the pages themselves.

Various H-driver power boards can be used, such as the common L298 dual H module, the LMD18200 module or IBT2 module.  The author also designed a system board to integrate these elements on along with an INA219 current monitor.  The board also supports an integrated LMD18200 providing a 4Amp maximum load.   Gerber files will be made available along with circuit schematics.

Does not support LocoNet or DCC++ at this time
Does not support DigiTrains throttle on mobile (this is websockets based)
