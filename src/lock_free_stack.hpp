#pragma once

#include <atomic>
#include <memory>

template <typename T>
class LockFreeStack {
 public:
  ~LockFreeStack() {
    while (pop()) {
    }
  }

  void push(const T& x) {
    ReferenceCount t;
    t.p = new Node(x);
    t.external_cnt = 1;
    // 下面比较中 release 保证之前的语句都先执行，因此 load 可以使用 relaxed
    t.p->next = head_.load(std::memory_order_relaxed);
    while (!head_.compare_exchange_weak(t.p->next, t, std::memory_order_release,
                                        std::memory_order_relaxed)) {
    }
  }

  std::shared_ptr<T> pop() {
    ReferenceCount t = head_.load(std::memory_order_relaxed);
    while (true) {
      increase_count(t);  // acquire
      Node* p = t.p;
      if (!p) {
        return nullptr;
      }
      if (head_.compare_exchange_strong(t, p->next,
                                        std::memory_order_relaxed)) {
        std::shared_ptr<T> res;
        res.swap(p->v);
        // 将外部计数减 2 后加到内部计数，减 2 是因为，
        // 节点被删除减 1，该线程无法再次访问此节点再减 1
        const int cnt = t.external_cnt - 2;
        // swap 要先于 delete，因此使用 release
        if (p->inner_cnt.fetch_add(cnt, std::memory_order_release) == -cnt) {
          delete p;  // 内外部计数和为 0
        }
        return res;
      }
      if (p->inner_cnt.fetch_sub(1, std::memory_order_relaxed) == 1) {
        p->inner_cnt.load(std::memory_order_acquire);  // 只是用 acquire 来同步
        // acquire 保证 delete 在之后执行
        delete p;  // 内部计数为 0
      }
    }
  }

 private:
  struct Node;

  struct ReferenceCount {
    int external_cnt;
    Node* p = nullptr;
  };

  struct Node {
    std::shared_ptr<T> v;
    std::atomic<int> inner_cnt = 0;
    ReferenceCount next;
    Node(const T& x) : v(std::make_shared<T>(x)) {}
  };

  void increase_count(ReferenceCount& old_cnt) {
    ReferenceCount new_cnt;
    do {  // 比较失败不改变当前值，并可以继续循环，因此可以选择 relaxed
      new_cnt = old_cnt;
      ++new_cnt.external_cnt;  // 访问 head_ 时递增外部计数，表示该节点正被使用
    } while (!head_.compare_exchange_strong(old_cnt, new_cnt,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed));
    old_cnt.external_cnt = new_cnt.external_cnt;
  }

 private:
  std::atomic<ReferenceCount> head_;
};
