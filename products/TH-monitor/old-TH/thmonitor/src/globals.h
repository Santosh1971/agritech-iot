#pragma once

#include <Arduino.h>

#include "THStore.hpp"

// Values persisted beyond restarts
struct StoredValues {
  long TempInterval;  // in seconds = 60; // every minute
  struct {
                // remains const/0 if the seqNum is not persisted
    unsigned int seqNum;  // seq num of TH messages
    long lastTs;  // time for next read TH; used only for planned restart's
  } THstatus;
  THStore::State THstore;
  void dump();
};

#include "EEPROMStorage.hpp"
// Manages non volatileValues in EEPROM
extern EEPROMStorage<StoredValues> EEPROMstorage;
