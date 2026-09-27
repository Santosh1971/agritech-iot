#pragma once

#include <Stream.h>

class MySerial {
  public:
    MySerial(Stream& serial, String name);

    MySerial(MySerial&) = delete;
    void operator=(MySerial&) = delete;

    /**
     * Reads a line if available
     * buf: where the read string should be copied (`\n` is not included)
     * Returns: true if a line is available and copied to buf; false otherwise
     */
    bool readLine(String &buf);

    /**
     * Flush any characters "available" in the stream
     * @return: #characters flushed out
     */
    int flushInput();
    void flushOutput() {
      serial.flush();
    }

    void writeLine(String buf); // write the line to the serial

    Stream& stream() {
      return serial;
    }

    String name;

    // dump a buffer for diagnostic purposes; especially when there are
    // non-printable characters in it
    // TODO: move this to a generic utils
    static void dump(String buf, const char* name="buf");

  private:
    Stream& serial;
    String buffer;
};
