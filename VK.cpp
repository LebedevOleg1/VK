#include <iostream>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <ctime>
#include <memory>

class Scheduler {
public:
    Scheduler() : running_(true), worker_([this] { this->WorkerLoop(); }) {}

    ~Scheduler() {
        Stop();
    }

    void Add(std::function<void()> task, std::time_t timestamp) {
        auto item = std::make_shared<TaskItem>(task, timestamp);

        std::lock_guard<std::mutex> lock(mutex_);
        bool notify = queue_.empty() || timestamp < queue_.top()->timestamp;
        queue_.push(item);
        if (notify) {
            cond_.notify_one();
        }
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cond_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    struct TaskItem {
        std::function<void()> task;
        std::time_t timestamp;

        TaskItem(std::function<void()> t, std::time_t ts)
            : task(std::move(t)), timestamp(ts) {}
    };

    struct Compare {
        bool operator()(const std::shared_ptr<TaskItem>& a,
                        const std::shared_ptr<TaskItem>& b) const {
            return a->timestamp > b->timestamp;
        }
    };

    std::priority_queue<std::shared_ptr<TaskItem>,
                        std::vector<std::shared_ptr<TaskItem>>,
                        Compare> queue_;

    std::mutex mutex_;
    std::condition_variable cond_;
    bool running_;
    std::thread worker_;

    void WorkerLoop() {
        std::unique_lock<std::mutex> lock(mutex_);

        while (running_) {
            if (queue_.empty()) {
                cond_.wait(lock, [this] { return !running_ || !queue_.empty(); });
                continue;
            }

            auto next_task_time = std::chrono::system_clock::from_time_t(queue_.top()->timestamp);

            if (cond_.wait_until(lock, next_task_time, [this, next_task_time] {
                return !running_ || queue_.empty() ||
                       std::chrono::system_clock::from_time_t(queue_.top()->timestamp) < next_task_time;
            })) {
                continue;
            }

            auto now = std::chrono::system_clock::now();
            while (!queue_.empty() &&
                   now >= std::chrono::system_clock::from_time_t(queue_.top()->timestamp)) {
                auto task_item = queue_.top();
                queue_.pop();

                lock.unlock();
                try {
                    if (task_item->task) {
                        task_item->task();
                    }
                } catch (...) {
                    // Игнорируем исключения
                }
                lock.lock();

                now = std::chrono::system_clock::now();
            }
        }
    }
};

int main() {
    Scheduler scheduler;

    auto now = std::time(nullptr);

    scheduler.Add([=]() {
        std::cout << "Task 1 executed at " << std::time(nullptr) << std::endl;
    }, now + 3);

    scheduler.Add([=]() {
        std::cout << "Task 2 executed at " << std::time(nullptr) << std::endl;
    }, now + 1);

    scheduler.Add([=]() {
        std::cout << "Task 3 executed at " << std::time(nullptr) << std::endl;
    }, now + 5);

    std::this_thread::sleep_for(std::chrono::seconds(7));
    scheduler.Stop();

    return 0;
}
