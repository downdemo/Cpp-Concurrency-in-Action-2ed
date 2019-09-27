#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

template <typename T>
class ConcurrentQueue {
 public:
  ConcurrentQueue() : head_(new Node), tail_(head_.get()) {}

  ConcurrentQueue(const ConcurrentQueue&) = delete;

  ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

  void push(T x) {
    auto new_val = std::make_shared<T>(std::move(x));
    auto new_node = std::make_unique<Node>();
    Node* new_tail_node = new_node.get();
    {
      std::lock_guard<std::mutex> l(tail_mutex_);
      tail_->v = new_val;
      tail_->next = std::move(new_node);
      tail_ = new_tail_node;
    }
    cv_.notify_one();
  }

  std::shared_ptr<T> try_pop() {
    std::unique_ptr<Node> head_node = try_pop_head();
    return head_node ? head_node->v : nullptr;
  }

  bool try_pop(T& res) {
    std::unique_ptr<Node> head_node = try_pop_head(res);
    return head_node != nullptr;
  }

  std::shared_ptr<T> wait_and_pop() {
    std::unique_ptr<Node> head_node = wait_pop_head();
    return head_node->v;
  }

  void wait_and_pop(T& res) { wait_pop_head(res); }

  bool empty() const {
    std::lock_guard<std::mutex> l(head_mutex_);
    return head_.get() == get_tail();
  }

 private:
  struct Node {
    std::shared_ptr<T> v;
    std::unique_ptr<Node> next;
  };

 private:
  std::unique_ptr<Node> try_pop_head() {
    std::lock_guard<std::mutex> l(head_mutex_);
    if (head_.get() == get_tail()) {
      return nullptr;
    }
    return pop_head();
  }

  std::unique_ptr<Node> try_pop_head(T& res) {
    std::lock_guard<std::mutex> l(head_mutex_);
    if (head_.get() == get_tail()) {
      return nullptr;
    }
    res = std::move(*head_->v);
    return pop_head();
  }

  std::unique_ptr<Node> wait_pop_head() {
    std::unique_lock<std::mutex> l(wait_for_data());
    return pop_head();
  }

  std::unique_ptr<Node> wait_pop_head(T& res) {
    std::unique_lock<std::mutex> l(wait_for_data());
    res = std::move(*head_->v);
    return pop_head();
  }

  std::unique_lock<std::mutex> wait_for_data() {
    std::unique_lock<std::mutex> l(head_mutex_);
    cv_.wait(l, [this] { return head_.get() != get_tail(); });
    return l;
  }

  std::unique_ptr<Node> pop_head() {
    std::unique_ptr<Node> head_node = std::move(head_);
    head_ = std::move(head_node->next);
    return head_node;
  }

  Node* get_tail() {
    std::lock_guard<std::mutex> l(tail_mutex_);
    return tail_;
  }

 private:
  std::unique_ptr<Node> head_;
  Node* tail_ = nullptr;
  std::mutex head_mutex_;
  mutable std::mutex tail_mutex_;
  std::condition_variable cv_;
};
