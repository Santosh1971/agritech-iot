#pragma once
#include <Arduino.h>

// Civil date helpers (Howard Hinnant's algorithms), no libc timezone use.
inline int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

inline void civilFromDays(int32_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y += m <= 2;
}

inline String dateOfDay(uint32_t day) {
  int y;
  unsigned m, d;
  civilFromDays((int32_t)day, y, m, d);
  char b[12];
  snprintf(b, sizeof b, "%04d-%02u-%02u", y, m, d);
  return b;
}

// "YYYY-MM-DD HH:MM:SS" for a wall-clock epoch.
inline String fmtLocal(uint32_t localEpoch) {
  uint32_t s = localEpoch % 86400u;
  char b[24];
  snprintf(b, sizeof b, "%s %02lu:%02lu:%02lu", dateOfDay(localEpoch / 86400u).c_str(), (unsigned long)(s / 3600),
           (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  return b;
}
