#include "WiFiClock.hpp"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#else
  #error("Unsupported platform: neither ESP8266 nor ESP32")
#endif

#include <WiFiUdp.h>

#define DEBUG
#include "debug.hpp"

static uint32_t timeout = 1500; // milli seconds

static const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
static byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets
static void sendNTPpacket(UDP& Udp, IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

Time WiFiClock::now(/*char* ntpServerName*/) {
  if (WiFi.status() != WL_CONNECTED) {
    TRACE("WiFi not connected: Clock not setup");
    return -1;
  }

  const char* ntpServerName = nullptr;
  if (ntpServerName == nullptr)
    ntpServerName = defaultNTPServer;

  TRACE("WiFiClock::now", ntpServerName);

  // CHECKME: whether this should be done only in setup
  WiFiUDP Udp;
  const unsigned int localPort = 8888;  // local port to listen for UDP packets
  TRACE("Starting UDP at port", localPort);
  Udp.begin(localPort);

  while (Udp.parsePacket() > 0) ; // discard any previously received packets

  IPAddress ntpServerIP; // NTP server's ip address
  WiFi.hostByName(ntpServerName, ntpServerIP);
  TRACE("NTPServerIP", ntpServerIP);

  sendNTPpacket(Udp, ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < timeout) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      TRACE("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];

      // CHECKME: why this correction is needed
const long timeZoneSecs = tzShiftInMinutes*60;
      return secsSince1900 - 2208988800UL + timeZoneSecs;
    }
  }
  WARN("No NTP Response :-(");
  return -1; // return 0 if unable to get the time
}
