#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace xbox::core {

class Executor {
 public:
  explicit Executor(std::size_t threads);
  ~Executor();
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  template <typename Callable>
  std::future<typename std::invoke_result_t<Callable>> submit(Callable callable) {
    using Result = typename std::invoke_result_t<Callable>;
    auto task = std::make_shared<std::packaged_task<Result()>>(std::move(callable));
    std::future<Result> future = task->get_future();
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (stopped_) throw std::runtime_error("executor stopped");
      tasks_.emplace([task]() { (*task)(); });
    }
    condition_.notify_one();
    return future;
  }

  std::size_t pending() const;
  void shutdown();

 private:
  void worker_loop();

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::queue<std::function<void()>> tasks_;
  std::vector<std::thread> workers_;
  bool stopped_ = false;
};

}  // namespace xbox::core
