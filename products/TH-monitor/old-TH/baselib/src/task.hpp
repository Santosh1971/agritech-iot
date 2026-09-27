#pragma once

/**
 * Library to manage execution of tasks resulting from processing of inputs
 * Tasks are buffered so that they dont intefere in inputs processing
 */

#include <functional>

#include <queue>

#include <Arduino.h>

/*
 * A task which might take multiple calls to complete
 * @return must return true to indicate that it is completed
 */
using Task=std::function<bool()>;

class TaskManager {
  private:
    struct Node {
      Task task;
      String name;
    };
    std::queue<Node> queue;  // TODO: back it up with the right sized container

  public:
    /**
     * Enqueue a task. If name is is given, it is printed for tracing
     */
    void enqueue(Task task, const char* name=nullptr);
    void enqueue(Task task, String name);

    /**
     * maxAttempts: #attempts to be tried
     * @return: #items performed and removed from the queue
     */
    int loop(int maxAttempts=1);
};

extern TaskManager taskManager;
