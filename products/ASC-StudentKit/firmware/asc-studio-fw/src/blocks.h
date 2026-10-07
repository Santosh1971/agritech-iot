// Drivers for each block in the studio's library: set up, read, self-test.
#pragma once
#include "design.h"

void blocksBegin(Design& d, const BoardMap& board);   // after a new design is loaded
void blocksRead(Design& d);                           // refresh every slot's values
void blocksSelfTest(Design& d, JsonArray results);    // one entry per slot
void blocksShow(const Design& d, const char* status); // OLED, if the design has one

// Reading for a rule's "S1" or "S3:t" reference. NAN if missing or failed.
float valueFor(Design& d, const char* ref);
// 1 = safe to run (float: water present, rain: dry), 0 = not safe, -1 = no reading.
int guardOk(Design& d, const char* port);
