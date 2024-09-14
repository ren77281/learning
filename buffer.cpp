#include <condition_variable>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>

// 对缓冲区的封装，用optional封装数据，nullopt表示空值
template <class T> class BoundedBuffer {
private:
  std::queue<std::optional<T>> q_;      // 用来存储数据
  size_t cap_;                          // 容量
  std::condition_variable producer_cv_; // 用来阻塞生产者的条件变量
  std::condition_variable consumer_cv_; // 用来阻塞消费者的条件变量
  std::mutex producer_mtx_;             // 保证生产者互斥的锁
  std::mutex consumer_mtx_;             // 保证消费者互斥的锁

public:
  BoundedBuffer(size_t cap) : cap_(cap) {}
  size_t size() { return q_.size(); }

  void push(const std::optional<T> &data) {
    std::unique_lock<std::mutex> lk(producer_mtx_);
    // 缓冲区不为满时，才能push
    producer_cv_.wait(lk, [&] { return q_.size() < cap_; });
    q_.push(data);
    lk.unlock();
    // 生产完，通知其他消费者
    consumer_cv_.notify_all();
  }

  std::optional<T> pop() {
    std::unique_lock<std::mutex> lk(consumer_mtx_);
    // 缓冲区不为空时，才能pop
    consumer_cv_.wait(lk, [&] { return !q_.empty(); });
    auto data = q_.front();
    q_.pop();
    lk.unlock();
    // 消费完，通知其他生产者
    producer_cv_.notify_all();
    return data;
  }
};

template <class T> void producer(BoundedBuffer<T> *buf) {
  for (int i = 0; i < 32; i++) {
    buf->push(i);
  }
}

// 调用一次终止一个消费者
template <class T> void producer_done(BoundedBuffer<T> *buf) {
  buf->push(std::nullopt);
}

template <class T> void consumer(BoundedBuffer<T> *buf, int id) {
  for (int i = 0; i < 32; i++) {
    auto data = buf->pop();
    // 空值则退出，否则消费
    if (data.has_value()) {
      std::cout << std::format("{} 消费了 {}\n", id, *data);
    } else {
      break;
    }
  }
}

int main() { 
  // 定义有界缓冲区
  BoundedBuffer<int> buf(10);
  std::jthread producer_thread(producer<int>, &buf);
  // 定义vector存储jthread，延长jthread的生命周期，使之在vector析构时调用join，而不是在for函数中调用join
  std::vector<std::jthread> consumer_threads;
  int consumer_cnt = 32;
  for (int i = 0; i < consumer_cnt; i++) {
    std::jthread consumer_thread(consumer<int>, &buf, i);
    consumer_threads.push_back(std::move(consumer_thread));
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  // 主线程终止消费者
  for (int i = 0; i < consumer_cnt; i++) {
    producer_done(&buf);
  }
  return 0; 
}