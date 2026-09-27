#pragma once

#include <Arduino.h>

#include <EEPROM.h>

#define DEBUG
#include "debug.hpp"

#ifdef DEBUG
#define EEPROM_DEBUG
#endif

extern size_t EEPROMStorage_totalSz;    // TODO: replace by EEPROM.length()

/**
 * Manage values which are saved into EEPROM
 * Refresh and save operations are via these values
 * Values should be set up by the caller 
 */
template <typename _Val>
class EEPROMStorage {
    const uint8_t marker;
    _Val storedValues;

    const int startIx;
    const size_t sz;

  public:
    static bool begin(size_t totalSz) {
      PRINTLN("EEPROM.begin(" + String(totalSz) + ")");

      bool rc = true;
#ifdef ESP8266
      EEPROM.begin(totalSz);
      if (totalSz > EEPROM.length()) {
        ERROR(F("EEPROM Size Requested "), totalSz, F("exceeds available"), EEPROM.length());
        rc = false;
      }
#elif ESP32
      if (!EEPROM.begin(totalSz)) {
        ERROR(F("EEPROM.begin("), totalSz, F(") failed"));
        rc = false;
      }
#else
  #error("Unsupported platform: neither ESP8266 nor ESP32")
#endif
      EEPROMStorage_totalSz = totalSz;

      if (totalSz < sizeof(storedValues) + sizeof(marker)) {
        WARN(F("EEPROM size"), totalSz, F("is too low; should be at least"),
              (sizeof(storedValues) + sizeof(marker)));
      }

      return rc;
    }

    static void begin() { // call begin with the right defaults
      begin(defaultSz());
    }

    const size_t defaultSz() {
      return sizeof(marker) + sizeof(storedValues);
    }

    /**
     * Setup marker, and size of EEPROM area
     * Derive default size from marker/storedValues size
     * StartIx defaults to beginning. Can be different
     */
    EEPROMStorage(const uint8_t marker, const int startIx=0, const size_t size=0):
      marker(marker), startIx(startIx),
      sz(size<=0? defaultSz(): size) {
      if (size < defaultSz()) {
        WARN(F("Storage size "), size, F(" is too low; should be at least"), defaultSz());
      }
      if (EEPROMStorage_totalSz < startIx + size) {
        WARN(F("Storage area ("), startIx, "," + size,
            F(") overflows the EEPROM size allocated"), EEPROMStorage_totalSz);
      }
    }

    _Val& values() {
      return storedValues;
    }

    /**
     * Size of the storage allocated
     */
    size_t size() {
      return sz;
    }

    /*
     * Start address of the storage; useful to have multiple storage areas
     */
    int startAddress() {
      return startIx;
    }

    /**
     * setup the EEPROM area
     * * if initialized as per marker, then overwrite values() with that
     * * otherwise, overwrite EEPROM area with contents of values(); and set marker
     * * User MUST setup values() with the defaults prior to calling this.
     *
     * @return true if values() were got from EEPROM; false otherwise
     * must be called once at the beginning
     */
    bool setup() {
      if (matchMarker()) {
        return refresh();
      }

      save(false); // save the default area; dont commit
      resetMarker();  // will also commit
      return false;
    }

    /**
     * @return true if marker is matched
     */
    bool matchMarker() {
      uint8_t tempMarker = 0xff;
      EEPROM.get(startIx, tempMarker);

      if (tempMarker != marker) {
        TRACE("EEPROM: invalid marker:" + String(tempMarker) + "!=" + marker);
        return false;
      } else {
        TRACE("EEPROM: Marker as expected:" + String(tempMarker));
        return true;
      }
    }

    /**
     * reset the marker in EEPROM to the expected value
     * @newMarker: if 0, use the standard one; else, use that
     *    in that case, in the next setup call, it will result in save
     */
    void resetMarker(uint8_t newMarker=0) {
      if (newMarker == 0)
        newMarker = marker;
      EEPROM.put(startIx, newMarker);
      commit();

      TRACE("EEPROM: Marker is now:" + String(newMarker));
    }

    /*
     * save the entire values to EEPROM
     * @bcommit: if true commit after save; see @save below
     */
    void save(bool bcommit=true) {
      save(storedValues, bcommit);
    }

    /*
     * save a member
     * @bcommit: if true, commit after save; send false to commit after saving
     * multiple members
     */
    template <typename T>
    void save(T& t, bool bcommit=true) {
      int offset = (uint8_t*)&t - (uint8_t*)&storedValues;
      if (offset < 0 || (offset+sizeof(t)) > sizeof(storedValues)) {
        WARN(F("EEPROM::save rejected; field is not part of the storage"));
        return;
      }
#ifdef EEPROM_DEBUG
      PRINT(F("EEPROM::save@"), startIx+1+offset, F("#"), sizeof(t));
#endif

      // TODO: if t is too big, write in small pieces, with yield()
      EEPROM.put(startIx+1+offset, t);
#ifdef EEPROM_DEBUG
      PRINTLN();
#endif
      if (bcommit) {
        delay(100); // NOTE: essential to avoid crashing
        commit();
      }
    }

    /**
     * refresh values from EEPROM
     * @returns true if valid values were got, and values refreshed
     */
    bool refresh() {
      return refresh(storedValues);
    }

    /*
     * save a member
     */
    template <typename T>
    bool refresh(T& t) {
      int offset = (uint8_t*)&t - (uint8_t*)&storedValues;
      if (offset < 0 || (offset+sizeof(t)) > sizeof(storedValues)) {
        WARN(F("EEPROM::refresh rejected; field is not part of the storage"));
        return false;
      }

#ifdef EEPROM_DEBUG
      PRINT(F("EEPROM::refresh@"), startIx+1+offset, "#", sizeof(t));
#endif
      // TODO: if t is too big, read in small pieces, with yield()
      EEPROM.get(startIx+1+offset, t);
#ifdef EEPROM_DEBUG
      PRINTLN(F(":successful"));
#endif
      return true;
    }

    void commit() {
#ifdef EEPROM_DEBUG
      PRINTLN("EEPROM:commit()ed");
#endif
      EEPROM.commit();
    }
};
