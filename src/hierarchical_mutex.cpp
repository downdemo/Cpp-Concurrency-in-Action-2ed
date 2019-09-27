#include <iostream>
#include <mutex>
#include <stdexcept>

class HierarchicalMutex {
 public:
  explicit HierarchicalMutex(int hierarchy_value)
      : cur_hierarchy_(hierarchy_value), prev_hierarchy_(0) {}

  void lock() {
    validate_hierarchy();  // 层级错误则抛异常
    m_.lock();
    update_hierarchy();
  }

  bool try_lock() {
    validate_hierarchy();
    if (!m_.try_lock()) {
      return false;
    }
    update_hierarchy();
    return true;
  }

  void unlock() {
    if (thread_hierarchy_ != cur_hierarchy_) {
      throw std::logic_error("mutex hierarchy violated");
    }
    thread_hierarchy_ = prev_hierarchy_;  // 恢复前一线程的层级值
    m_.unlock();
  }

 private:
  void validate_hierarchy() {
    if (thread_hierarchy_ <= cur_hierarchy_) {
      throw std::logic_error("mutex hierarchy violated");
    }
  }

  void update_hierarchy() {
    // 先存储当前线程的层级值（用于解锁时恢复）
    prev_hierarchy_ = thread_hierarchy_;
    // 再把其设为锁的层级值
    thread_hierarchy_ = cur_hierarchy_;
  }

 private:
  std::mutex m_;
  const int cur_hierarchy_;
  int prev_hierarchy_;
  static thread_local int thread_hierarchy_;  // 所在线程的层级值
};

// static thread_local 表示存活于一个线程周期
thread_local int HierarchicalMutex::thread_hierarchy_(INT_MAX);

HierarchicalMutex high(10000);
HierarchicalMutex mid(6000);
HierarchicalMutex low(5000);

void lf() {  // 最低层函数
  std::lock_guard<HierarchicalMutex> l(low);
  // 调用 low.lock()，thread_hierarchy_ 为 INT_MAX，
  // cur_hierarchy_ 为 5000，thread_hierarchy_ > cur_hierarchy_，
  // 通过检查，上锁，prev_hierarchy_ 更新为 INT_MAX，
  // thread_hierarchy_ 更新为 5000
}  // 调用 low.unlock()，thread_hierarchy_ == cur_hierarchy_，
// 通过检查，thread_hierarchy_ 恢复为 prev_hierarchy_ 保存的 INT_MAX，解锁

void hf() {
  std::lock_guard<HierarchicalMutex> l(high);  // high.cur_hierarchy_ 为 10000
  // thread_hierarchy_ 为 10000，可以调用低层函数
  lf();  // thread_hierarchy_ 从 10000 更新为 5000
  //  thread_hierarchy_ 恢复为 10000
}  //  thread_hierarchy_ 恢复为 INT_MAX

void mf() {
  std::lock_guard<HierarchicalMutex> l(mid);  // thread_hierarchy_ 为 6000
  hf();  // thread_hierarchy_ < high.cur_hierarchy_，违反了层级结构，抛异常
}

int main() {
  lf();
  hf();
  try {
    mf();
  } catch (std::logic_error& ex) {
    std::cout << ex.what();
  }
}
