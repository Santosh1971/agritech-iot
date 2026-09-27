#pragma once

#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#else
  #error("Unsupported platform: neither ESP8266 nor ESP32")
#endif

using Time=long;

class MiscUtils {
  public:
    MiscUtils() = delete;

    static String macToStr(const uint8_t* mac);

    static String ipToString(IPAddress ip);

    static String formMAC();  // a string from "my MAC"

#if 0
    static String currentDateTime(); // "Jan 21 2018 - 21:41:39";
#endif

    /**
     * convert the date/time to a standard ISO string format in GMT timezone
     * eg. 2019-08-11T13:06:43.006Z
     * @time: if not passed, use current date
     * @return String format: e.g. 
     */
    static String timeToString(Time time);
    static String timeToString();

    static void setCurrentTime(unsigned int yyyy, unsigned int mm, unsigned int dd, unsigned int hh, unsigned int min, unsigned int ss);

    static String basename(String fullpath, char separator='\\');  // return last part of a path

    /**
     * Return characters following the last '.'
     * Return empty if there is no '.' (or no characters are following the last '.'
     */
    static String getExtension(String filename);

    /**
     * Remove the last '.' and all characters following it
     */
    static String getNameWithoutExtension(String filename);

    /**
     * make up version string from the version file as per standard conventions
     */
    static String buildVersionString(String filename, bool pfxGsmOrWifi=true);

    static bool parseURL(String url, String& protocol, String& host, int& port, String& uri);

    static void printDeviceInfo(const char* deviceId="");

    /**
     * assorted device generic setup
     */
    static void setup();

    static bool setPowerBoostKeepOn(int en);
};
