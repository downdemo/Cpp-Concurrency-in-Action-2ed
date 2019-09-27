* 设计并发数据结构要考虑两点，一是确保访问 thread-safe，二是提高并发度
  * thread-safe 基本要求如下
    * 数据结构的不变量（invariant）被一个线程破坏时，确保不被线程看到此状态
    * 提供操作完整的函数来避免数据结构接口中固有的 race condition
    * 注意数据结构出现异常时的行为，以确保不变量不被破坏
    * 限制锁的范围，避免可能的嵌套锁，最小化死锁的概率
  * 作为数据结构的设计者，要提高数据结构的并发度，可以从以下角度考虑
    * 部分操作能否在锁的范围外执行
    * 数据结构的不同部分是否被不同的 mutex 保护
    * 是否所有操作需要同级别的保护
    * 在不影响操作语义的前提下，能否对数据结构做简单的修改提高并发度
  * 总结为一点，即最小化线程对共享数据的轮流访问，最大化真实的并发量

## thread-safe queue

* 之前实现过的 thread-safe stack 和 queue 都是用一把锁定保护整个数据结构，这限制了并发性，多线程在成员函数中阻塞时，同一时间只有一个线程能工作。这种限制主要是因为内部实现使用的是 [std::queue](https://en.cppreference.com/w/cpp/container/queue)，为了支持更高的并发，需要更换内部的实现方式，使用细粒度的（fine-grained）锁。最简单的实现方式是包含头尾指针的单链表，不考虑并发的单链表实现如下

```cpp
#include <memory>
#include <utility>

template <typename T>
class Queue {
 public:
  Queue() = default;

  Queue(const Queue&) = delete;

  Queue& operator=(const Queue&) = delete;

  void push(T x) {
    auto new_node = std::make_unique<Node>(std::move(x));
    Node* new_tail_node = new_node.get();
    if (tail_) {
      tail_->next = std::move(new_node);
    } else {
      head_ = std::move(new_node);
    }
    tail_ = new_tail_node;
  }

  std::shared_ptr<T> try_pop() {
    if (!head_) {
      return nullptr;
    }
    auto res = std::make_shared<T>(std::move(head_->v));
    std::unique_ptr<Node> head_node = std::move(head_);
    head_ = std::move(head_node->next);
    return res;
  }

 private:
  struct Node {
    explicit Node(T x) : v(std::move(x)) {}
    T v;
    std::unique_ptr<Node> next;
  };

  std::unique_ptr<Node> head_;
  Node* tail_ = nullptr;
};
```

* 即使用两个 mutex 分别保护头尾指针，这个实现在多线程下也有明显问题。push 可以同时修改头尾指针，会对两个 mutex 上锁，另外仅有一个元素时头尾指针相等，push 写和 try_pop 读的 next 节点是同一对象，产生了竞争，锁的也是同一个 mutex
* 该问题很容易解决，在头节点前初始化一个 dummy 节点即可，这样 push 只访问尾节点，不会再与 try_pop 竞争头节点

```cpp
#include <memory>
#include <utility>

template <typename T>
class Queue {
 public:
  Queue() : head_(new Node), tail_(head_.get()) {}

  Queue(const Queue&) = delete;

  Queue& operator=(const Queue&) = delete;

  void push(T x) {
    auto new_val = std::make_shared<T>(std::move(x));
    auto new_node = std::make_unique<Node>();
    Node* new_tail_node = new_node.get();
    tail_->v = new_val;
    tail_->next = std::move(new_node);
    tail_ = new_tail_node;
  }

  std::shared_ptr<T> try_pop() {
    if (head_.get() == tail_) {
      return nullptr;
    }
    std::shared_ptr<T> res = head->v;
    std::unique_ptr<Node> head_node = std::move(head_);
    head_ = std::move(head_node->next);
    return res;
  }

 private:
  struct Node {
    std::shared_ptr<T> v;
    std::unique_ptr<Node> next;
  };

  std::unique_ptr<Node> head_;
  Node* tail_ = nullptr;
};
```

* 接着加上锁，锁的范围应该尽可能小

```cpp
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

    std::lock_guard<std::mutex> l(tail_mutex_);
    tail_->v = new_val;
    tail_->next = std::move(new_node);
    tail_ = new_tail_node;
  }

  std::shared_ptr<T> try_pop() {
    std::unique_ptr<Node> head_node = pop_head();
    return head_node ? head_node->v : nullptr;
  }

 private:
  struct Node {
    std::shared_ptr<T> v;
    std::unique_ptr<Node> next;
  };

 private:
  std::unique_ptr<Node> pop_head() {
    std::lock_guard<std::mutex> l(head_mutex_);
    if (head_.get() == get_tail()) {
      return nullptr;
    }
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
  std::mutex tail_mutex_;
};
```

* push 中创建新值和新节点都没上锁，多线程可用并发创建新值和新节点。虽然同时只有一个线程能添加新节点，但这只需要一个指针赋值操作，锁住尾节点的时间很短，try_pop 中对尾节点只是用来做一次比较，持有尾节点的时间同样很短，因此 try_pop 和 push 几乎可以同时调用。try_pop 中锁住头节点所做的也只是指针赋值操作，开销较大的析构在锁外进行，这意味着虽然同时只有一个线程能 pop_head，但允许多线程删除节点并返回数据，提升了 try_pop 的并发调用数量
* 最后再结合 [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable) 实现 wait_and_pop，即得到与之前接口相同但并发度更高的 thread-safe queue

```cpp
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
```

## thread-safe map

* 并发访问 [std::map](https://en.cppreference.com/w/cpp/container/map) 和 [std::unordered_map](https://en.cppreference.com/w/cpp/container/unordered_map) 的接口的问题在于迭代器，其他线程删除元素时会导致迭代器失效，因此 thread-safe map 的接口设计就要跳过迭代器
* 为了使用细粒度锁，就不应该使用标准库容器。可选的关联容器数据结构有三种，一是二叉树（如红黑树），但每次查找修改都要从访问根节点开始，也就表示根节点需要上锁，尽管沿着树向下访问节点时会解锁，但这个比起覆盖整个数据结构的单个锁好不了多少
* 第二种方式是有序数组，这比二叉树还差，因为无法提前得知一个给定的值应该放在哪，于是同样需要一个覆盖整个数组的锁
* 第三种方式是哈希表。假如有一个固定数量的桶，一个 key 属于哪个桶取决于 key 的属性和哈希函数，这意味着可以安全地分开锁住每个桶。如果使用读写锁，就能将并发度提高相当于桶数量的倍数

```cpp
#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

template <typename K, typename V, typename Hash = std::hash<K>>
class ConcurrentMap {
 public:
  // 桶数默认为 19（一般用 x % 桶数作为 x 的桶索引，桶数为质数可使桶分布均匀）
  ConcurrentMap(std::size_t n = 19, const Hash& h = Hash{})
      : buckets_(n), hasher_(h) {
    for (auto& x : buckets_) {
      x.reset(new Bucket);
    }
  }

  ConcurrentMap(const ConcurrentMap&) = delete;

  ConcurrentMap& operator=(const ConcurrentMap&) = delete;

  V get(const K& k, const V& default_value = V{}) const {
    return get_bucket(k).get(k, default_value);
  }

  void set(const K& k, const V& v) { get_bucket(k).set(k, v); }

  void erase(const K& k) { get_bucket(k).erase(k); }

  // 为了方便使用，提供一个到 std::map 的映射
  std::map<K, V> to_map() const {
    std::vector<std::unique_lock<std::shared_mutex>> locks;
    for (auto& x : buckets_) {
      locks.emplace_back(std::unique_lock<std::shared_mutex>(x->m));
    }
    std::map<K, V> res;
    for (auto& x : buckets_) {
      for (auto& y : x->data) {
        res.emplace(y);
      }
    }
    return res;
  }

 private:
  struct Bucket {
    std::list<std::pair<K, V>> data;
    mutable std::shared_mutex m;  // 每个桶都用这个锁保护

    V get(const K& k, const V& default_value) const {
      // 没有修改任何值，异常安全
      std::shared_lock<std::shared_mutex> l(m);  // 只读锁，可共享
      auto it = std::find_if(data.begin(), data.end(),
                             [&](auto& x) { return x.first == k; });
      return it == data.end() ? default_value : it->second;
    }

    void set(const K& k, const V& v) {
      std::unique_lock<std::shared_mutex> l(m);  // 写，单独占用
      auto it = std::find_if(data.begin(), data.end(),
                             [&](auto& x) { return x.first == k; });
      if (it == data.end()) {
        data.emplace_back(k, v);  // emplace_back 异常安全
      } else {
        it->second = v;  // 赋值可能抛异常，但值是用户提供的，可放心让用户处理
      }
    }

    void erase(const K& k) {
      std::unique_lock<std::shared_mutex> l(m);  // 写，单独占用
      auto it = std::find_if(data.begin(), data.end(),
                             [&](auto& x) { return x.first == k; });
      if (it != data.end()) {
        data.erase(it);
      }
    }
  };

  Bucket& get_bucket(const K& k) const {  // 桶数固定因此可以无锁调用
    return *buckets_[hasher_(k) % buckets_.size()];
  }

 private:
  std::vector<std::unique_ptr<Bucket>> buckets_;
  Hash hasher_;
};
```

## thread-safe list

```cpp
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
```
