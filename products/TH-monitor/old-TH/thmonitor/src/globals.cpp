#include "globals.h"

#include "myTimer.hpp"
MyTimer timers;

#include "platform.hpp"
// shares same marker; stored immediately after the platform config
EEPROMStorage<StoredValues> EEPROMstorage(PLATFORM_STORAGE_MARKER, platformConfigstorage.size());

#include "debug.hpp"

void StoredValues::dump() {
    PRINT("Stored{");
    PRINT(TempInterval);
    PRINT(",");
    PRINT("Seq#:");
    PRINT(THstatus.seqNum);
    PRINT(",");
    PRINT("Store:");
    PRINT(THstore.toString());
    PRINT("}");
}
