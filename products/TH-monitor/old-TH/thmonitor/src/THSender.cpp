#include "THSender.hpp"

#include "myDHT.hpp"

#include "task.hpp"

#include "MQTTMessenger.hpp"

#include "calendarUtils.hpp"

#include "globals.h"

#define DEBUG
#include "debug.hpp"

THSender::THSender(THStore& store): store(store) {
}

static void logTH(const char* msg, float temp, float humi, long time) {
  TRACE("TH::", msg, temp, humi, CalendarUtils::toString(time));
}

static void logTH(const char* msg, THRow& row) {
  logTH(msg, row.rec.temp, row.rec.hum, row.time);
}

void THSender::setReadCycle(unsigned long interval) {
  if (readTimer.active()) {
    TRACE("Stopping read cycle");
    readTimer.stop();
  }

  if (interval == 0) {
    TRACE("No read cycle, since interval is", interval);
    return;
  }

  TRACE("Starting read cycle", interval);
  readTimer = timers.repeat(interval, [=]() {
    readSendTH();

    EEPROMstorage.values().THstatus.lastTs = CalendarUtils::now();
    // save it only if there is a planned restart
    return true;
  });
}

#include "globals.h"

/**
 * Determining Sequence Number:
 * If persisting in EEPROM, use that.
 * If not:
 *   * When connected, use the THseqNum
 *   * When not connected, postpone it
 */

static unsigned int nextSeqNum() {
      const int MAX_SEQNUM = 65535;

      auto next = ++EEPROMstorage.values().THstatus.seqNum;
      if (next > MAX_SEQNUM) {
        next = EEPROMstorage.values().THstatus.seqNum = 1;
      }
#ifdef PERSIST_THSEQNUM
      EEPROMstorage.save(EEPROMstorage.values().THstatus.seqNum);
#endif

      return  next;
}

void THSender::sendFromStore() {
  if (store.isEmpty())
    return;

  if (!mqttMessenger.isConnected()) {  // no longer connected
    TRACE("Not connected; attempt again later");
    scheduleSendAttempt();
    return;
  }

  taskManager.enqueue([this]() {
    bool rc = store.fetch([=](THRow& row) {
      auto& rec = row.rec;
      logTH("Sending offline", rec.temp, rec.hum, row.time);

      mqttMessenger.sendTH(rec.temp, rec.hum, row.time, nextSeqNum(), false);  // enqueued in task manager

#if 0
      timers.after(1, [this]() {  // force 1 second before next row is processed
        sendFromStore();
      });
#else
      delay(100); // this will add to overall wait
      sendFromStore();
#endif

      return true;
    });

    return true;
  });
}

void THSender::readSendTH() {
  float temp = -1, humi = -1;
  if (!myDHT.read(temp, humi)) {
    return; // nothing read
  }
    
  auto now = CalendarUtils::now();

  if (store.isEmpty() && mqttMessenger.isConnected()) {  // send directly
    logTH("Sending live", temp, humi, now);
    mqttMessenger.sendTH(temp, humi, now, nextSeqNum(), true); // enqueued in task manager
    return;
  }

  if (store.isFull()) { // delete one row
    WARN(F("THStore is full; discarding a row"));
    store.fetch([](THRow& row) {
      logTH("Discard an old row", row);
      return true;
    });
  }

  THRow row{ temp, humi, now};
  logTH("Saving offline", row);
  store.save(row);

  scheduleSendAttempt();
}

static const unsigned long MQTT_CONNECT_INTERVAL = 60; // 1 minute
void THSender::scheduleSendAttempt() {
  if (sendTimer.active()) {
    TRACE("THSender::send attempt already scheduled: ignored");
    return;
  }

  sendTimer = timers.after(MQTT_CONNECT_INTERVAL, [this]() {
      sendFromStore();
    });
  TRACE("THSender::send attempt scheduled");
}
