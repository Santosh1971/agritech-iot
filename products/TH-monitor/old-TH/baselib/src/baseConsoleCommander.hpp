#pragma once

#include "mySerial.hpp"

/**
 * Note: new commands can be added by simply extending this class,
 * and overriding process().
 */
class BaseConsoleCommander {
  MySerial cmdSerial;

public:
  BaseConsoleCommander();

  bool loop();  // return true if any command was processed

  /**
   * a command is identified by a single char, and
   * can have an args string in a format specifif to the command
   * @return true if cmd was processed (successfully or otherwise)
   */
  virtual bool process(char cmd, String args);
};
