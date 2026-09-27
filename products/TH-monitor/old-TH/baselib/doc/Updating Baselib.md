# Requirements

The base library, called `baselib` here onwards, contains most of the common
code for configuring, setting up and using MQTT, GSM/WiFi, RTC, OTA and other
essential common functions.

Whenever `baselib` gets updated, the specific update has to be used in the
products, presently in ESP32, from VS Code. (Presenly, `thmonitor2` and `SMController`).

# Updating Base library

1. Git source and branch of `baselib` is as defined in `platformio.ini`

2. `baselib` is downloaded from the Git source automatically by platformio as
   part of build process.

3. `baselib` is updated from the Git source, usimg the `Update project
   libraries` tasks of `platformIO`.
   This is described in `Using PlatformIO.md` document.
