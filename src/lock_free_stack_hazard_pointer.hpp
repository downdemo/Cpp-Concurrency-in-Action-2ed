#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

static constexpr std::size_t MaxSize = 100;

struct HazardPointer {
  std::atomic<std::thread::id> id;
  std::atomic<void*> p;
};

static HazardPointer HazardPointers[MaxSize];

class HazardPointerHelper {
 public:
  HazardPointerHelper() {
    for (auto& x : HazardPointers) {
      std::thread::id default_id;
      if (x.id.compare_exchange_strong(default_id,
                                       std::this_thread::get_id())) {
        hazard_pointer = &x;  // 取一个未设置过的风险指针
        break;
      }
    }
    if (!hazard_pointer) {
      throw std::runtime_error("No hazard pointers available");
    }
  }

  ~HazardPointerHelper() {
    hazard_pointer->p.store(nullptr);
    hazard_pointer->id.store(std::thread::id{});
  }

  HazardPointerHelper(const HazardPointerHelper&) = delete;

  HazardPointerHelper operator=(const HazardPointerHelper&) = delete;

  std::atomic<void*>& get() { return hazard_pointer->p; }

 private:
  HazardPointer* hazard_pointer = nullptr;
};

std::atomic<void*>& hazard_pointer_for_this_thread() {
  static thread_local HazardPointerHelper t;
  return t.get();
}

bool is_existing(void* p) {
  for (auto& x : HazardPointers) {
    if (x.p.load() == p) {
      return true;
    }
  }
  return false;
}

template <typename T>
class LockFreeStack {
 public:
  void push(const T& x) {
    Node* t = new Node(x);
    t->next = head_.load();
    while (!head_.compare_exchange_weak(t->next, t)) {
    }
  }

  std::shared_ptr<T> pop() {
    std::atomic<void*>& hazard_pointer = hazard_pointer_for_this_thread();
    Node* t = head_.load();
    do {  // 外循环确保 t 为最新的头节点，循环结束后将头节点设为下一节点
      Node* t2;
      do {  // 循环至风险指针保存当前最新的头节点
        t2 = t;
        hazard_pointer.store(t);
        t = head_.load();
      } while (t != t2);
    } while (t && !head_.compare_exchange_strong(t, t->next));
    hazard_pointer.store(nullptr);
    std::shared_ptr<T> res;
    if (t) {
      res.swap(t->v);
      if (is_existing(t)) {
        append_to_delete_list(new DataToDelete{t});
      } else {
        delete t;
      }
      try_delete();
    }
    return res;
  }

 private:
  struct Node {
    std::shared_ptr<T> v;
    Node* next = nullptr;
    Node(const T& x) : v(std::make_shared<T>(x)) {}
  };

  struct DataToDelete {
   public:
    template <typename T>
    DataToDelete(T* p)
        : data(p), deleter([](void* p) { delete static_cast<T*>(p); }) {}

    ~DataToDelete() { deleter(data); }

    void* data = nullptr;
    std::function<void(void*)> deleter;
    DataToDelete* next = nullptr;
  };

 private:
  void append_to_delete_list(DataToDelete* t) {
    t->next = to_delete_list_.load();
    while (!to_delete_list_.compare_exchange_weak(t->next, t)) {
    }
  }

  void try_delete() {
    DataToDelete* cur = to_delete_list_.exchange(nullptr);
    while (cur) {
      DataToDelete* t = cur->next;
      if (!is_existing(cur->data)) {
        delete cur;
      } else {
        append_to_delete_list(new DataToDelete{cur});
      }
      cur = t;
    }
  }

 private:
  std::atomic<Node*> head_;
  std::atomic<std::size_t> pop_cnt_;
  std::atomic<DataToDelete*> to_delete_list_;
};
