#include "payload.hpp"

#include <cmath>

namespace Payload {
// see https://arduinojson.org/v6/how-to/configure-the-serialization-of-floats/
  double round(double f, uint8_t decimalplaces) {
    double corr = pow(10, decimalplaces);
    return (int)(corr * f + 0.5)/corr;
  }
};
