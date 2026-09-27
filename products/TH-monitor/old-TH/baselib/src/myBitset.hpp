#pragma once

#include <Arduino.h>

//#include <bitset.h>

class MyBitset {
    unsigned long v;

  public:
    MyBitset(): v(0) {
    }

    MyBitset(String& bstring): v(0) {
		    set(bstring);
    }
    
    MyBitset(const char* s): MyBitset() {
      String bstring(s);
      set(bstring);
    }

    String asString() {  // upto 32 bits
      return String(v, BIN);
    }

    unsigned long value() {
      return v;
    }

    bool none() {
      return v == 0;
    }

    bool any() {
      return v != 0;
    }

    /*
     * size(), all(): variable; not supporting
     */

    void set(String& s) {
      if (s == "null") {  // TODO: move this to the caller
        return;
      }

      unsigned long out = 0;

      for (unsigned int i =0; i < s.length(); i++) {
        char c = s[i];
        switch(c) {
          case '0':
          case '1':
            out = (out << 1) | (c - '0');
            break;
          default:
            Serial.print("**WARNING**: Non-bit char '");
            Serial.print(String(c));
            Serial.print("' in bitstring '");
            Serial.print(s);
            Serial.println("'; the string is not used");
            return;
        }
      }

      v = out;

#if 0
      Serial.println("MyBitset(" + s + ")=");
      Serial.println("        '" + asString() + "'");
#endif
    }

    void set(int bitNo, bool on) {
      bitWrite(v, bitNo, (on? 1: 0));
    }

    bool get(int bitNo) {
      return bitRead(v, bitNo);
    }

    bool operator[](int bitNo) {
      return get(bitNo);
    }
};
