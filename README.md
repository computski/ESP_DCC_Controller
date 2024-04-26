# ESP_DCC_Controller

+++++++ UPDATED 2024-04-26 ++++++++++++++++++
Now uses LittleFS and the latest version 7 ArduinoJson library

DCC controller for model railroad control based on the nodeMCU ESP12-E module.  This is an ESP8266 device (ESP12), not an ESP32.

Note: see https://www.instructables.com/member/Computski/instructables/ for a detailed description of the project and build instructions.

Implements a DCC controller and JRMI server which supports mobile clients running EngineDriver (android) or WiThrottle (IOS). To allow a lower cost build, it is not necessary to fit the optional 4 x 4 keypad matrix, Jogwheel and 1602 LCD display.

Generates a DCC signal as a low level routine.  This is fed with DCC packets from higher level routines which handle keyboard/display
and communications over TCP with the JRMI throttles (on mobile phones).

Current monitoring and saftey trip is supported via a INA219 current monitor.

The system also supports a web interface giving a means to control the current and voltage safety trip levels, as well as parameters such
as SSID and IP address.  The web interface also supports editing of locomotive and turnout rosters - these can also be modified via the
keypad, but this may not be fitted by the builder if they opt for a mobile only version.

Similarly locomotive decoder programming is supported on the unit, both service mode and POM, but JRMI throttles don't support this, so 
it is also provided on a web interface.

The web interface consists of static HTML pages served through a webserver - these pages are held in the data directory and this must also be uploaded to the target device.  Interactivity on these pages is provied via Websockets and Javascript on the pages themselves.

Various H-driver power boards can be used, such as the common L298 dual H module, the LMD18200 module or IBT2 module.  The author also designed a system board to integrate these elements on along with an INA219 current monitor.  The board also supports an integrated LMD18200 providing a 4Amp maximum load.   Gerber files will be made available along with circuit schematics.

Does not support LocoNet or DCC++ at this time
Does not support DigiTrains throttle on mobile (this is websockets based)
I did port the code to an ESP32 but there are problems with other libraries (notably websockets) which cause unacceptable operational problems.

2021-11-01 added DC support, system can run as a simple PWM DC controller and in which case it will respond only to loco 3 and 28 speed steps
and added a definition for the DOIT ESP12 motor shield.  This is a cheap L293 shield that fits the nodeMCU. requires an external INA219 module.

2021-12-07 added support for a WeMos D1R1 board and L298 motor shield.  Simple, 2 boards, 1 PSU and no soldering.  See Computski channel on youTube.

2022-03-31 added support for HW40 rotary encoder types for Jogwheel.  This is configured in Global.h

2023-05-07:  Arduino 2.0 IDE does not support data-upload to the ESP boards (see https://forum.arduino.cc/t/tools-for-arduino-ide-2-0-beta/894962/2) you can only use
IDE versions to 1.8x
