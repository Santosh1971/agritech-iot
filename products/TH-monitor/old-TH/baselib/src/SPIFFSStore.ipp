#include "SPIFFSStore.hpp"

#ifdef ESP8266
#include <FS.h>
#elif ESP32
#include <SPIFFS.h>
#else
  #error("Unsupported platform: neither ESP8266 nor ESP32")
#endif

#define DEBUG
#include "debug.hpp"


template <typename Row>
SPIFFSStore<Row>::SPIFFSStore(String filepath):
  filepath(filepath), 
  back(state.indexes.back), front(state.indexes.front)
{
}

template <typename Row>
bool SPIFFSStore<Row>::isEmpty() {
  return front.index < 0;
}

template <typename Row>
bool SPIFFSStore<Row>::isFull() {
  return back.index < 0;
}

#if 0
template <typename Row>
int SPIFFSStore<Row>::count() {
  if (back.index < 0) // nothing empty
    return limit*2;
  if (front.index < 0)  // nothing full
    return 0;

  int count = 0;
  count += limit - front.index;
  count += back.index;
  if (front.file1 == back.file1) {

  int c = back.index - front.index;
  if (c < 0)
    c += limit;
  if (back.file1 != front.file1)
    c += limit;

  return c;
}
#endif

/**
 * FIXME: separate instances for each template; strictly incorrect
 */
static bool begin() {
  static bool begun = false;
  static bool rc;
  if (begun)
    return rc;
#ifdef ESP8266
  rc = SPIFFS.begin();
#elif ESP32
  rc = SPIFFS.begin(true);  // true=>autformat on failure
#endif
  begun = true;
  if (!rc) {
    ERROR("FS:can't mount filesystem!");
  }
  return rc;
}

template <typename Row>
void SPIFFSStore<Row>::setup(std::function<void (State& newState, bool onlyIndexChanged)> onChange, State* initialState){
  if (!begin()) {
    // TODO: what now?
  }

  this->onChange = onChange;

  if (initialState != nullptr)
    state = *initialState;

  TRACE("SPIFFStore::setup", (initialState == nullptr)? String("<>"): initialState->toString());
}

template <typename Row>
void SPIFFSStore<Row>::reset() {
  back = { false, 0 };
  front = { false, -1 };

  saveIndexes();
}

template <typename Row>
bool SPIFFSStore<Row>::save(Row& row) {
  TRACE("FS::save", row.toString());

  if (back.index <= 0) { // discard existing contents before appending
    truncate(back.file1);
  }

  // write the record at `back`
  String path = filename(back.file1);
  File file = SPIFFS.open(path, "a");
  if (!file) {
    ERROR("Opening file", path);
    return false;
  }

  auto bytes = file.write((const uint8_t *)&row, sizeof(row));
  file.close();

  if (bytes != sizeof(row)) {
    ERROR("Writing row in file ", path, "at index", back.index, "#bytes written", bytes, "size", sizeof(row), getUsage().toString());
    return false;
  }

  TRACE("wrote", bytes, "bytes at", back.toString());

  if (++back.index >= state.limit) {
    back = { !back.file1, 0 };  // alternate file

    if (front.file1 == back.file1) { // discard all these
      front = { !back.file1, 0 };
    }
  }
  if (front.index < 0) { // it was empty
    front = { back.file1, back.index-1};
    if (front.index < 0) {
      front = {!back.file1, state.limit-1};  // last row of alternate file
    }
  }

  saveIndexes();

  return true;
}

template <typename Row>
bool SPIFFSStore<Row>::fetch(std::function<bool (Row& row)> handle) {
  if (front.index < 0) {
    // TRACE("FS::Fetch: Empty", state.toString());
    return false;
  }

  String path = filename(front.file1);
  File file = SPIFFS.open(path, "r");
  if (!file) {
    ERROR("Error opening file for read", path, "cannot fetch");
    return false;
  }

  int pos = front.index * sizeof(Row);
  file.seek(pos, SeekSet);
  Row row;
  auto bytes = file.readBytes((char*)&row, sizeof(Row));
  TRACE("read", bytes, "bytes at", front.toString());

  if (bytes != sizeof(row)) {
    ERROR("Reading row in file ", path, "at pos", pos, "#bytes read", bytes, "size", sizeof(row));
    ERROR("File:size", file.size(), "available", file.available());
    file.close();
    return false;
  }

  file.close();

  TRACE("FS::fetch()ed", row.toString());
  bool rc = handle(row);
  TRACE("Handle returned", rc);

  if (!rc)
    return rc;  // no further change

  // record is processed; empty it
  if (++front.index >= state.limit) {
    front = { !front.file1, 0 };
  }
  if (front == back) {
    front.index = -1; // empty
  }

  saveIndexes();

  return rc;
}

template <typename Row>
typename SPIFFSStore<Row>::Usage SPIFFSStore<Row>::getUsage() {
  if (!begin()) { // might be called quite early
    return Usage();
  }

#ifdef ESP8266
  FSInfo info;
  if (!SPIFFS.info(info)) {
    WARN("SPIFFS.info returned false");
    return Usage();
  }
  return Usage(info.totalBytes, info.usedBytes);
#else
  return Usage(SPIFFS.totalBytes(), SPIFFS.usedBytes());
#endif
}

template <typename Row>
void SPIFFSStore<Row>::setLimit(int limit) {
  if (limit <= 0) {
    ERROR("SPIFFSStore::limit cannot be set to a negative integer", limit);
    return;
  }

  if (state.limit == limit) {
    TRACE("No change in limit", limit);
    return;
  }

  if (limit > maxLimit()) {
    ERROR("Limit exceeded SPIFFS capacity; ignored", limit, sizeof(Row), maxLimit());
    return;
  }
  
  state.limit = limit;
  if (onChange != nullptr)
    onChange(state, false);

  reset();

  WARN("SPIFFSStore reset to", limit, "rows; any existing rows will be discarded");
}

template <typename Row>
int SPIFFSStore<Row>::maxLimit() {
  int maxBytes = getUsage().totalBytes;
                                          // Uses 2 files to keep the queue
  int limit = (maxBytes > 0)? ((maxBytes / sizeof(Row))/2): 0;

  TRACE("SPIFFSStore::maxLimit", limit, sizeof(Row), maxBytes);

  return limit;
}

template <typename Row>
bool SPIFFSStore<Row>::truncate(bool which) {
  auto path = filename(which);
  if (!SPIFFS.exists(path))
    return false;

  TRACE("FS::remove", path);
  SPIFFS.remove(path);
  return true;
}

template <typename Row>
void SPIFFSStore<Row>::saveIndexes() {
  TRACE("SPIFFStore::saveIndexes", front.toString(), back.toString());

  if (onChange != nullptr)
    onChange(state, true);
}

template <typename Row>
String SPIFFSStore<Row>::filename(bool which) {
  return filepath + (which? "2": "1");
}
