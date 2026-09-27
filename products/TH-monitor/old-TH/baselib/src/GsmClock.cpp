#include "GsmClock.hpp"

#include <RTClib.h> // for DateTime

#define DEBUG
#include "debug.hpp"

#include <stdio.h>
GsmClock::GsmClock(MyGsm&myGsm): myGsm(myGsm) {
}

Time GsmClock::now() {
  auto& modem = myGsm._modem();

  if (!clockSet) {
    auto rc = setup();
    if (rc == nullptr) {
      TRACE("GSMClock setup");
      clockSet = true;
    } else {
      WARN("GSMClock setup error:", rc);
      return -1;
    }
  }

#ifdef TINY_GSM_MODEM_SIM800
  String date = modem.getGSMDateTime(DATE_FULL);
  TRACE("GsmFactory::getTime", date);

  int yy, MM, dd, hh, mm, ss, zz; // yy/MM/dd,hh:mm:ss+/-zz
  int rc = sscanf(date.c_str(), "%2u/%2u/%2u,%2u:%2u:%2u%3d",
      &yy, &MM, &dd, &hh, &mm, &ss, &zz);
  if (rc != 7) {
    WARN(F("Invalid date value?"), date, F("scan returned"), rc);
    return -1;
  }
  if (yy < 19) {
    WARN(F("GSMDateTime is probably not set: ignored"), date);
    return -1;
  }

  DateTime dt(2000 + yy, MM, dd, hh, mm, ss);
  /**
   * Note: Here, zz sometimes contains 0, sometimes 22. How?
   * Ignore zz.
   * Date is always returned in the "local" TZ, handled by RTClib.
   */
  return dt.unixtime() /* - zz *15*60 */; // zz=tz in quarters of hour
#endif

  return -1;
}

const char* GsmClock::setup() {
  auto& modem = myGsm._modem();

#ifdef TINY_GSM_MODEM_SIM800
  modem.sendAT(GF("+CLTS=1"));  // Enable auto network time sync
  if (modem.waitResponse(10000L) != 1) {
    return "CLTS:Enable network time sync";
  } 

  // #TZ shifts in slots of 15 minutes
  const int TZslots = tzShiftInMinutes/15; // this will be 22
  // String s = GF(R"(+CNTP=")") + String(defaultNTPServer) + R"(",22)";
  String s = GF(R"(+CNTP=")") + String(defaultNTPServer) + R"(",)" + TZslots;
  modem.sendAT(s.c_str());  // set the NTP server
  if (modem.waitResponse() != 1) {
    return "CNTP:Set GSM NTP server";
  }
#if 1
  // Without the below command, it seems to fail. Why?!
  modem.sendAT(GF("+CNTP?")); // check if it is set
  modem.waitResponse();
#endif
  delay(1000);  // just wait for the modem
  modem.sendAT(GF("+CNTP"));
  if (modem.waitResponse() != 1) {
    return "CNTP:CNTP command";
  }
  delay(1000);  // just wait for the modem

  #define GSM_NL "" // FIXME: check what this should be

  if (modem.waitResponse(10000, GF(GSM_NL "+CNTP:")) != 1) {
    return "CNTP:Sync with GSM NTP server";
  } 
  delay(1000);  // just wait for the modem

  // TODO: check status code=1

  modem.sendAT(GF("+CLTS=1"));  // Enable auto network time sync
  if (modem.waitResponse(10000L) != 1) {
    return "CLTS:Enable network time sync";
  } 

  modem.sendAT(GF("&W"));       // Save configuration
  if (modem.waitResponse() != 1) {
    return "&W: Save configuration";
  }

  modem.sendAT(GF("+CCLK?"));
  if (modem.waitResponse(2000L, GF(GSM_NL "+CCLK: \"")) != 1) {
    return "CCLCK?: Restart and check time";
  }

  return nullptr;
#endif

  return "Unsupported modem";
}
