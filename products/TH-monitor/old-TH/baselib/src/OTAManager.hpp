#pragma once

#include <Arduino.h>

#include <functional>

/**
 * Generic placeholder: implemented separately for GSM and WiFi
 */
class OTAManager {
  protected:
    const char* otaUrl;
    const char* defaultFilename;

  public:
    /**
     * Usually called OTAManager("http://prodapp.agrisensorsandcontrols.com:8080/FOTA", __FILE__)
     *  where the first parameter is the standard place where the OTA file is
     *  placed, and second is the path of the ino file.
     */
    OTAManager (const char* otaUrl, const char* defaultFilename);

    virtual void setup(void)=0; // call once during setup

    /**
     * Handle intermittent messages on upload happenings and final result/error
     */
    using Messenger = std::function<void (String event, String detail)>;

    /**
     * start OTA
     * @filename: if different from defaultFilename; of the form abcd.ino
     * @messenger: Messenger; optional
     */
    virtual void start(const char* filename=nullptr, Messenger messenger=nullptr)=0; // call once to start the OTA process

    virtual void loop(void)=0;  // should be called from loop

  protected:
    /*
     * form full URL of the FOTA from just the filename
     * @filename: if nullptr, use the defaultFilename
     * @return the full URL
     */
    String buildFotaUrl(const char* filename=nullptr);
};
