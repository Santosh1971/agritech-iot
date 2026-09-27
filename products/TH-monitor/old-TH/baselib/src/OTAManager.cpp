#include "OTAManager.hpp"

#include "miscUtils.hpp"

#define DEBUG
#include "debug.hpp"

OTAManager::OTAManager(const char* otaUrl, const char* defaultFilename):
  otaUrl(otaUrl), defaultFilename(defaultFilename)
{
  // PRINTLN("OTA:", otaUrl, defaultFilename);
}

String OTAManager::buildFotaUrl(const char* filename)
{
  if (filename == nullptr)
    filename = defaultFilename;

  String name = MiscUtils::basename(filename, '/');
  String extn = MiscUtils::getExtension(name);
  extn.toLowerCase();
  if (extn == "cpp") { // new style
    name = MiscUtils::getNameWithoutExtension(name);
  }

  return String(otaUrl) + "/" + name + ".bin";
}
