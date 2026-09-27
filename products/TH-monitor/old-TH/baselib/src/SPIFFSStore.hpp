#pragma once

#include <Arduino.h>

#include <FS.h>

#include <functional>

/**
 * # Storage format
 * Each row of type <Row> is stored in binary form in fixed size.
 * - so its contents should be of fixed size (data type such as String wont work)
 * - a method toString() is required for debug logging/tracing
 *
 * Max #rows is as per limit, in each file.
 *
 * # Circular Queue
 * Circular queue spanning file1 and file2 (so, when pushing hit the limit
 * on one file, it can jump to the other file rewriting it.)
 *
 * # Indexes
 * front: { bool file1; int index; } // index of first of block of nonempty rows
 * back: { bool file1; int index; } // index of first of block of empty rows
 * -+++|++--: {true,1},{false,2}
 * ----|----: {true,-1},{true,0}
 * ++++|++++: {true,0},{true,-1}
 */
template <typename Row>
class SPIFFSStore {
public:
  /**
   */
  struct _Ix {
    bool file1; // which file: 1 or 2
    int index;  // index within that file
    bool operator==(const struct _Ix& other) {
      return other.file1 == file1 && other.index == index;
    }
    String toString() {
      return String("{") + file1 + "," + index + "}";
    }
  };

  /*
   * State is maintained in EEPROM also
   * Alternate: could be in SPIFFS; but EEPROM is preferred. Why?
   */
  struct State {
    int limit;
    struct {
      _Ix front; // ... of first of block of non-empty rows
      _Ix back; // ... of first of block of empty rows
    } indexes;

    State(int limit=0): limit(limit), indexes{ { false, -1 }, { false, 0 } } {
    }

    String toString() {
      return String("{")
          + limit + "," + indexes.front.toString() + "," + indexes.back.toString()
        + "}";
    }
  };

  SPIFFSStore(String filepath);

  /**
   * @return true if store is empty
   */
  bool isEmpty(); 

  /**
   * @return true if store has hit limit
   */
  bool isFull();

#if 0
  /**
   * actual rows present in the store
   */
  int count();
#endif

  /**
   * one time setup
   * * onChange: call it whenver state changes; used to save change in EEPROM
   * * initialState: as read in from EEPROM
   * Decouples from EEPROM; but, would be good to internalize the coupling
   */
  void setup(std::function<void (State& newState, bool onlyIndexChanged)> onChange, State* initialState);

  /**
   * set limit
   */
  void setLimit(int limit);

  struct Usage {
    int totalBytes = -1;
    int usedBytes = 1;
    Usage() {
    }
    Usage(int total, int used): totalBytes(total), usedBytes(used) {
    }
    String toString() {
      return String("Usage{Total/Used:") + totalBytes + "/" + usedBytes + "}";
    };
  };
  static Usage getUsage();
  
  /**
   * Compute maximum possible according to SPIFFS capacity
   */

  static int maxLimit();
  /**
   * reset contents and indices to empty state
   */
  void reset();

  /**
   * save a log at end of store.
   * @return true on success; false on otherwise such as when hitting limit
   */
  bool save(Row& row);

  /**
   * if handle returns true, then fetched row is removed from store.
   * else is made available for next call.
   * when store is empty, the positions are reset.
   * @return true if any row is fetched and processed.
   */
  bool fetch(std::function<bool (Row& row)> handle);

  // TODO: kludge
  State getState() { return state; } // copy of the state

private:
  String filepath;
  State state;

  _Ix& back;  // two convenient references
  _Ix& front;

  std::function<void (State& newState, bool onlyIndexChanged)> onChange;

  void saveIndexes();

  /**
   * CAUTION: "w" mode doesnt seem to truncate the file; so explicitly remove
   */
  bool truncate(bool which); // truncate if existing

  String filename(bool which);
};
