#pragma once

#include <memory>
#include <mutex>
#include <utility>

template <typename T>
class ConcurrentList {
 public:
  ConcurrentList() = default;

  ~ConcurrentList() {
    remove_if([](const Node&) { return true; });
  }

  ConcurrentList(const ConcurrentList&) = delete;

  ConcurrentList& operator=(const ConcurrentList&) = delete;

  void push_front(const T& x) {
    std::unique_ptr<Node> t(new Node(x));
    std::lock_guard<std::mutex> head_lock(head_.m);
    t->next = std::move(head_.next);
    head_.next = std::move(t);
  }

  template <typename F>
  void for_each(F f) {
    Node* cur = &head_;
    std::unique_lock<std::mutex> head_lock(head_.m);
    while (Node* const next = cur->next.get()) {
      std::unique_lock<std::mutex> next_lock(next->m);
      head_lock.unlock();  // 锁住了下一节点，因此可以释放上一节点的锁
      f(*next->data);
      cur = next;                        // 当前节点指向下一节点
      head_lock = std::move(next_lock);  // 转交下一节点锁的所有权，循环上述过程
    }
  }

  template <typename F>
  std::shared_ptr<T> find_first_if(F f) {
    Node* cur = &head_;
    std::unique_lock<std::mutex> head_lock(head_.m);
    while (Node* const next = cur->next.get()) {
      std::unique_lock<std::mutex> next_lock(next->m);
      head_lock.unlock();
      if (f(*next->data)) {
        return next->data;  // 返回目标值，无需继续查找
      }
      cur = next;
      head_lock = std::move(next_lock);
    }
    return nullptr;
  }

  template <typename F>
  void remove_if(F f) {
    Node* cur = &head_;
    std::unique_lock<std::mutex> head_lock(head_.m);
    while (Node* const next = cur->next.get()) {
      std::unique_lock<std::mutex> next_lock(next->m);
      if (f(*next->data)) {  // 为 true 则移除下一节点
        std::unique_ptr<Node> old_next = std::move(cur->next);
        cur->next = std::move(next->next);  // 下一节点设为下下节点
        next_lock.unlock();
      } else {  // 否则继续转至下一节点
        head_lock.unlock();
        cur = next;
        head_lock = std::move(next_lock);
      }
    }
  }

 private:
  struct Node {
    std::mutex m;
    std::shared_ptr<T> data;
    std::unique_ptr<Node> next;
    Node() = default;
    Node(const T& x) : data(std::make_shared<T>(x)) {}
  };

  Node head_;
};
