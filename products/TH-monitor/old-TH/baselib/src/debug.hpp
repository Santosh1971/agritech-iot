#pragma once

#include <Arduino.h>

extern Stream& Console;

// General purpose output routine to direct output to console
#ifdef SUPPRESS_PRINT
  #define PRINT(...)
  #define PRINTLN(...)
#else
namespace {
  template<typename T>
  static void PRINT(T last) {
    Console.print(last);
  }

  template<typename T, typename... Args>
  static void PRINT(T head, Args... tail) {
    Console.print(head);
    Console.print(' ');
    PRINT(tail...);
  }

  template<typename T>
  static inline void PRINTLN(T last) {
    Console.println(last);
  }

  static inline void PRINTLN() {
    Console.println();
  }

  template<typename T, typename... Args>
  static void PRINTLN(T head, Args... tail) {
    Console.print(head);
    Console.print(' ');
    PRINTLN(tail...);
  }
}
#endif

#define _NL '\n'

//#define DEBUG

#ifdef DEBUG
namespace {
  template<typename... Args>
  static void TRACE(Args... tail) {
    PRINT(tail..., _NL);
  }
}
#else
  #define TRACE(...)
#endif

namespace {
  template<typename... Args>
  static void WARN(Args... tail) {
    PRINT(F("***WARNING***"), tail..., _NL);
  }

  // for backward  compatibility, define WARING also similarly
  template<typename... Args>
  static void WARNING(Args... tail) {
    PRINT(F("***WARNING***"), tail..., _NL);
  }

  template<typename... Args>
  static void ERROR(Args... tail) {
    PRINT(F("***ERROR***"), tail..., _NL);
  }
}

#ifdef DEBUG
#define D(_x) { Console.print(__LINE__); Console.print(">"); Console.println(#_x); }
#define Dln Console.println
#else
#define D(_x)
#define Dln
#endif
