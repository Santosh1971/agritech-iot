# Baselib

## Overview

Baselib is intended to capture common requirements across all our controller firmware.
A few assumptions/limitations:

  * It runs on ESP8266 or ESP32
  * WiFi or GSM are supported for connectivity to Gomos platform
    - It is a build-time switch. But, even then both WiFi & GSM specific code
      might be included
    - Intent is that, _in future_, it will be a configuration-time switch.
      And, later on a run-time switch, so it might use either WiFi or GSM
      whichever is connectible.
  * Not a very complete design. Scope for improvement galore.

## Key Functionality

  * MQTT based connectivity to Gomos/platform, over GSM or WiFi
  * OTA installation
  * RTC, with automatic clock synchronization with network based clocks
  * A few other utilities such as:
    - C++ template libraries for EEPROM storage and SPIFFS storage
    - LED control
    - Timers
    - Common commands and messages over MQTT
    - Common commands over serial console
    - Calendar
    - debug tracing, error logging

# Key Classes

Class/File | Description | Remarks
---|---|---
**Main Classes**
platform.cpp|Main class holding together baselib|Switch between WiFi/GSM, manage messages from/to Gomos, setup Clock/OTA/Network connection
platformConfig.cpp|Platform's own configuration saved in EEPROM across reboots
MQTTManager.cpp|Main class for MQTT connection, publish/subscribe
OTAmanager.cpp|(OOPS) parent of GSMOTAManager and WiFiOTAManager
RTCClock.cpp| RTC based clock with options to synchronize with an external clock|External clock from network is provided by GsmFactory or WiFiFactory
myClient.hpp|Internal class of platform|(OOPS) parent of a network connected client implemented by myGsm and myWiFi
**Main Classes extended for GSM**
GsmClock.cpp|GSM based clock to return `now` time|Implements `MyClock` defined in `timeUtils`
GsmFactory.cpp|Returns GSM specific classes for MQTT client, clock, OTA etc.| See WiFiFactory for its WiFi sibling
GsmOTAManager.cpp|OTA manager for GSM
myGsm.cpp|GSM connected client|(OOPS) extends MyClient
**Main Classes extended for WiFi**
WiFiClock.cpp|Network based clock provider over WiFi connector|Implements `MyClock` defined in `timeUtils`
WiFiFactory.cpp|Returns WiFi specific classes for MQTT client, clock, OTA etc.| See GSM Factory for its GSM  sibling
WiFiOTAManager.cpp|OTA Manager for WiFi
myWiFi.cpp|WiFi connected client|(OOPS) extends MyClient
myWiFiAPs.cpp|Helper for WiFi to manage & save multiple SSID/passwords
myWiFiManager.cpp|Wraps over WiFiManager with additional functions|Connection LED indicator, multiple access points
**Assorted Utilities**
EEPROMStorage.cpp|Template library to map and manipulate EEPROM areas as just C++ data structures
LEDController.cpp|Setup, turn off/on, blink an LED
LEDSRController.cpp|Same as LED Controller; but connected via Shift-register|Configuration might be hard-coded
SPIFFSStore.ipp|C++ template library for storing a queue of fixed-size `Row`s|Rows for sending status to platform are buffered here till MQTT connection
calendarUtils.cpp|Combines `millis()` with RTC clock to reduce calls to RTC|Utility for string<->time conversion over RTCClock
debug.h|TRACE and error logging utility
millisTimer.hpp|start, check and stop a `millis()` based timer
miscUtils.cpp|Assorted utility methods|Extract file names, time->printable string, mac related etc.
myBitset.hpp|Utility class to manage a set of bits
mySerial.cpp|Reads and writes a line of commands from/to a serial|Send/receive commands from serial lines
payload.hpp|Wrapper over `ArduinoJson.h` for both StaticJson and DynamicJson|Adds methods for convert from/to string, adding common fields such as timestamp/macId
task.cpp|A singleton task manager for early freeing of CPU|manages a queue of tasks; executes max 1 task in a loop()
timeUtils.cpp|Defines common time classes| including `MyClock` the (OOPS) parent of `GSMClock` and `WiFiClock`
myTimer.cpp|Manage timer tasks which can be stopped/Limitations: CalendarUtils/seconds based; upto 50 timer tasks
timer.h|An arduino library of timer tasks customized and used in platform|Used by myTimer<br/>Normally, we should avoid such customization
**Customizable Classes**|These are extended/modified by each product as needed.
baseConsoleCommander.cpp|Common commands over console for platform|Reboot, set WiFi parameters, switch between Dev/Test/Prod modes, erase or print EEPROM storage etc.
baseMQTTMessenger.cpp|Base class for product-specific messaging to platform|Also supplies common messages such as ack, error, text events, system info.
baseMQTTProcessor.cpp|Base class for product-specific processing of messages from platform|Also supplies processors for common messages such as OTA, Reboot etc.
topicMgr.cpp|Publish/subscribe topic builder|Helps in matching ack-topic to that of received message etc.
