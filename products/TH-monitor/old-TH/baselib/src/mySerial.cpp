#include "mySerial.hpp"

#include <Arduino.h>   // for yield

#define DEBUG
#include "debug.hpp"

MySerial::MySerial(Stream& serial, String name):
  name(name), serial(serial),
  buffer("                                                                                                          ")
{
  buffer = "";  // Mimics old code
}

bool MySerial::readLine(String &output) {
#if 0
  // not supported
  if (serial.overflow()) {
    WARN(name + ": input overflow detected; potential loss of messages");
  }
#endif
    while (serial.available()) {
      char inChar = (char)serial.read();  // get the new byte:  
       yield();

       if (inChar != '\n') {
         buffer += inChar;   // add it to the inputString:       
         continue;
       }

       // return the buffer to the caller; clear the buffer; `\n` is skipped
       output = buffer;
       buffer = "";

       TRACE(name, F("::Read#"), output.length(), F("'"), output, F("'"));
       return true;
    }

    return false;    // a full line is not available
}

void MySerial::writeLine(String s) {
  TRACE(name, F("::write#"), s.length(), F("'"), s, F("'"));
  auto rc = serial.println(s);
  if (rc < s.length()) {
    WARN(F("Message truncated while sending: only"), rc, F(" bytes sent"));
    WARN(F("Message was:"), s);
  }
}

int MySerial::flushInput() {
  int count=0;
  while(serial.available()) {                            
    (void)serial.read();
    ++count;
  }
  TRACE(name, F("::flushInput"), count, F("characters"));
  return count;
}

void MySerial::dump(String s, const char* name) {
#define P PRINT
  P(name); P("{"); P("#");
  P(s.length()); P("'");
  P(s); P("'[");
  auto delim="";
  for (unsigned int i=0; i<s.length(); ++i) {
    char c = s.charAt(i);
    P(delim); P(c); P("#"); P(String((int)c, HEX));
    delim=",";
  }
  P("]}");
#undef P
}
