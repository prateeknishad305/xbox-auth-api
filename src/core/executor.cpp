#include "core/executor.h"

#include <stdexcept>

namespace xbox::core {

Executor::Executor(std::size_t threads) {
  if (threads == 0) threads = 1;
  workers_.reserve(threads);
  for (std::size_t index = 0; index < threads; ++index) {
    workers_.emplace_back([this]() { worker_loop(); });
  }
}

Executor::~Executor() {
  shutdown();
}

void Executor::worker_loop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait(lock, [this]() { return stopped_ || !tasks_.empty(); });
      if (stopped_ && tasks_.empty()) return;
      task = std::move(tasks_.front());
      tasks_.pop();
    }
    try {
      task();
    } catch (...) {
    }
  }
}

std::size_t Executor::pending() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return tasks_.size();
}

void Executor::shutdown() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (stopped_) return;
    stopped_ = true;
  }
  condition_.notify_all();
  for (std::thread& worker : workers_) {
    if (worker.joinable()) worker.join();
  }
  workers_.clear();
}

}  // namespace xbox::core
