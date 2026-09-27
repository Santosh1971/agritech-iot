# Background
Recently reported issue from the field that, when the device went into a
configuration mode, the AP was not getting visible, despite multiple efforts.
This renders the device unuseable.

# Requirements
1. Fix to making the configuration mode being effective in all cases.
2. Alternative option if it is not found to be working.

# Analysis

## Root Cause

Multiple WiFi access points in a given area can clash,
when they use the same channel within the 2.5GHz band.
https://www.expertreviews.co.uk/networks/1401371/how-to-extend-wi-fi-range-increase-speed-and-fix-problems/page/0/1

WiFiManager presently always use channel 1.
So, if and when there is a clash, it will repeatedly fail.
That could be a probable cause of why the AP was not getting visible.

Fix will be to have WiFiManager to randomize channel selection
when getting into config mode each time.
Presently, WiFiManager forces channel to be 1, and doesnt allow it to change.
There is no easy change, without enhancing WiFiManager.

## Alternate Options
Alternative option is to provide an alternate channel for configuring the AP.
The obvious option is using the serial port.
There are the below considerations:

1. Following configurations will fall into scope of this.
    - WiFi access points directly
    - GSM/GPRS access points (apn, ...)
    - MQTT broker addresses and ports
    - Time
    - Product specific configurations
    - Product stage: dev/product/test

2. These could be set via different methods such as platform, serial ports etc.]Need/value for which/by what method to be discussed.

3. Technically, this has to be standardized across products.
There has to be device-side components,
complemented by a platform-side field utility (as described under `Device Commander` below), used to control and monitor the device in the field.

# Specifications

1. Command for directly specifying WiFi access point is implemented in `Feature/WiFiSettingOverride`. (to be integrated with `Test/v02` branch and released).

2. `WiFiManager` doesn't provide flexibility in channel selection.
Unless we hack to create our own version, till the project provides that
flexibility.

3. `Device Commander` utility for configuring and monitoring devices will
   consist of:

  * A [PWA](https://en.wikipedia.org/wiki/Progressive_web_applications), which can work offline from a laptop or mobile

  * The application drives the device via a serial connection using [WebUSB interface](https://developer.mozilla.org/en-US/docs/Web/API/USB).

      > This is an experimental interface; available only in Chrome and Opera

  * The application does the following:
    - provides UI for sending commands to device
      - custom UI for each command, or
      - ad hoc text
    - traps console output of the device, and displays on the page

# Long Term Considerations
1. `WiFiManager`:

  * Make it non-blocking

  * Allow flexibility in channel selection

2. The current `debugAssist.cpp` as a proper device monitor/commander.
