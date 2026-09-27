#include "miscUtils.hpp"

String VERSION() {
  String file = MiscUtils::basename(__FILE__);
  int lastDot = file.lastIndexOf('.');

  String version = file.substring(0, lastDot);  // remove .cpp

  int delimit = version.lastIndexOf('_'); // replace '_' by '/'
  if (delimit > 0) {
    version = version.substring(0, delimit) + '/' + version.substring(delimit+1);
  }

  return version;
}
