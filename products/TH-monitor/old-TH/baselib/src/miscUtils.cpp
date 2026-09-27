#include "miscUtils.hpp"

String MiscUtils::macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    //    if (i < 5)
    //      result += ':';
  }
  return result;
}

String MiscUtils::ipToString(IPAddress ip) {
  String s = "";
  for (int i = 0; i < 4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}

String MiscUtils::formMAC() {
  uint8_t MAC_array[6];
  WiFi.macAddress(MAC_array);

  String out = "";
  char temp[3];
  for (unsigned int i = 0; i < sizeof(MAC_array); ++i) {
    sprintf(temp, "%02x", MAC_array[i]);
    out = out + temp;
  }

  return out;
}

#define DEBUG
#include "debug.hpp"

#include "calendarUtils.hpp"

String MiscUtils::timeToString() {
  Time t = CalendarUtils::now();
  if (t < 0) { // cannot determine now
    return "<NullTS>";
  };
  return timeToString(t);
}

static const long timezoneOffset = tzShiftInMinutes * 60; // in seconds

String MiscUtils::timeToString(Time time) {
  DateTime tm(time - timezoneOffset);

  char s[30];
  /* int rc = */sprintf(s, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
    tm.year(), tm.month(), tm.day(),
    tm.hour(), tm.minute(), tm.second(), 0);

  return String(s);
}

#include "platform.hpp"

void MiscUtils::setCurrentTime(unsigned int yyyy, unsigned int mm, unsigned int dd, unsigned int hh, unsigned int min, unsigned int ss) {
   TRACE("Date ,Time : YYYY/MM/DD/ HH:mm:SS : " + String(yyyy) +"/" + String(mm) +"/" + String(dd) +" " + String(hh) +":" + String(min) +":" + String(ss) );

   DateTime dt(yyyy, mm, dd, hh, min, ss);
   Time time = dt.unixtime();

   platform.getClock().adjust(time);
   CalendarUtils::synchronize(true);
   TRACE("setCurrentTime to", CalendarUtils::toString(time));
}

String MiscUtils::basename(String fullpath, char separator)
{  // return last part of a path
        int lastslash = fullpath.lastIndexOf(separator);
        return fullpath.substring(lastslash+1);
}

String MiscUtils::getExtension(String filename)
{
  int lastdot = filename.lastIndexOf('.');
  if (lastdot < 0)
    return "";
  return filename.substring(lastdot+1);
}

String MiscUtils::getNameWithoutExtension(String filename)
{
  int lastdot = filename.lastIndexOf('.');
  if (lastdot < 0)
    return filename;
  return filename.substring(0, lastdot);
}

String MiscUtils::buildVersionString(String filename, bool pfxGsmOrWifi)
{
  String version = getNameWithoutExtension(basename(filename));

  int delimit = version.lastIndexOf('_'); // replace '_' by '/'
  if (delimit > 0) {
    version = version.substring(0, delimit) + '/' + version.substring(delimit+1);
  }

  if (pfxGsmOrWifi)
    version =
#ifdef USE_GSM
      "GSM"
#else
      "WiFi"
#endif
      + version;

  return version;
}

bool MiscUtils::parseURL(String url, String& protocol, String& host, int& port, String& uri)
{
  int index = url.indexOf(':');
  if(index < 0) {
    return false;
  }

  protocol = url.substring(0, index);
  url.remove(0, (index + 3)); // remove protocol part

  index = url.indexOf('/');
  String server = url.substring(0, index);
  url.remove(0, index);       // remove server part

  index = server.indexOf(':');
  if(index >= 0) {
    host = server.substring(0, index);          // hostname
    port = server.substring(index + 1).toInt(); // port
  } else {
    host = server;
    if (protocol == "http") {
      port = 80;
    } else if (protocol == "https") {
      port = 443;
    }
  }

  if (url.length()) {
    uri = url;
  } else {
    uri = "/";
  }
  return true;
}

void MiscUtils::printDeviceInfo(const char* deviceId)
{
  PRINTLN();
  PRINTLN("--------------------------");
  PRINTLN(String("Build:    ") +  __DATE__ " " __TIME__);
#if defined(ESP8266)
  PRINTLN(String("Flash:    ") + ESP.getFlashChipRealSize() / 1024 + "K");
  PRINTLN(String("ESP core: ") + ESP.getCoreVersion());
  PRINTLN(String("FW info:  ") + ESP.getSketchSize() + "/" + ESP.getFreeSketchSpace() + ", " + ESP.getSketchMD5());
#elif defined(ESP32)
  PRINTLN(String("Flash:    ") + ESP.getFlashChipSize() / 1024 + "K");
  PRINTLN(String("ESP sdk:  ") + ESP.getSdkVersion());
  PRINTLN(String("Chip rev: ") + ESP.getChipRevision());
#endif
  PRINTLN(String("Free mem: ") + ESP.getFreeHeap());
  PRINTLN(F("MacID"), formMAC());
  PRINTLN(F("Device ID"), deviceId);
  PRINTLN("--------------------------");
}

#include <Wire.h>

#define IP5306_ADDR          0x75
#define IP5306_REG_SYS_CTL0  0x00

bool MiscUtils::setPowerBoostKeepOn(int en)
{
  Wire.beginTransmission(IP5306_ADDR);
  Wire.write(IP5306_REG_SYS_CTL0);
  if (en) {
    Wire.write(0x37); // Set bit1: 1 enable 0 disable boost keep on
  } else {
    Wire.write(0x35); // 0x37 is default reg value
  }
  return Wire.endTransmission() == 0;
}

#include "platformConfig.hpp"

void MiscUtils::setup()
{
  auto& config = PlatformConfig::buildTime.misc;

  TRACE("Wire.begin", config.sdaPin, config.sclPin);
  Wire.begin(config.sdaPin, config.sclPin);

  TRACE("IP5306 KeepOn", setPowerBoostKeepOn(1));
}
