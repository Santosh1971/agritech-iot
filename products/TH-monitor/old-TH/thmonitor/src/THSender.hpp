#pragma once

#include "myTimer.hpp"

#include "THStore.hpp"

class THSender {
  private:
    MyTimer::Handle readTimer, sendTimer;
    THStore& store;

  public:
    THSender(THStore& store);

    void setReadCycle(unsigned long interval);

    void readSendTH(); // read and send TH once

    void sendFromStore();

  private:

    void scheduleSendAttempt();
};
