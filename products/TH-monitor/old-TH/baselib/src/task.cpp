#include "task.hpp"

#include <Arduino.h>

//#define DEBUG
#include "debug.hpp"

const int MAX = 10;

void TaskManager::enqueue(Task task, const char* name) {
  enqueue(task, String(name==nullptr? "<nul>": name));
}

void TaskManager::enqueue(Task task, String name) {
  if (queue.size() >= MAX) { // Too many; pop an task
    WARN(F("TaskMgr::Task queue size exceeded"), queue.size());
    // TODO: dequeue and throw away one
  }

  queue.push({task, name});
  TRACE(F("TaskMgr::queued:#"), queue.size(), name);
}


int TaskManager::loop(int maxAttempts) {
  int count = 0;
  for (int ix=0; ix<maxAttempts; ++ix) {
    if (queue.size() == 0)
      break;

    auto node = queue.front();
    TRACE(F("TaskMgr::perform#"), (ix+1), node.name);
    if (!node.task()) {
      TRACE(F("TaskMgr::incomplete task"), node.name);
      continue;  // will try again
    }

    queue.pop();
    ++count;
  }
  return count;
}

TaskManager taskManager;
