## 线程间共享数据存在的问题

* 不变量（invariant）：关于一个特定数据结构总为 true 的语句，比如 `双向链表的两个相邻节点 A 和 B，A 的后指针一定指向 B，B 的前指针一定指向 A`。有时程序为了方便会暂时破坏不变量，这通常发生于更新复杂数据结构的过程中，比如删除双向链表中的一个节点 N，要先让 N 的前一个节点指向 N 的后一个节点（不变量被破坏），再让 N 的后节点指向前节点，最后删除 N（此时不变量重新恢复）
* 线程修改共享数据时，就会发生破坏不变量的情况，此时如果有其他线程访问，就可能导致不变量被永久性破坏，这就是 race condition
* 如果线程执行顺序的先后对结果无影响，则为不需要关心的良性竞争。需要关心的是不变量被破坏时产生的 race condition
* C++ 标准中定义了 data race 的概念，指代一种特定的 race condition，即并发修改单个对象。data race 会造成未定义行为
* race condition 要求一个线程进行时，另一线程访问同一数据块，出现问题时很难复现，因此编程时需要使用大量复杂操作来避免 race condition

## 互斥锁（mutex）

* 使用 mutex 在访问共享数据前加锁，访问结束后解锁。一个线程用特定的 mutex 锁定后，其他线程必须等待该线程的 mutex 解锁才能访问共享数据
* C++11 提供了 [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex) 来创建一个 mutex，可通过 [lock](https://en.cppreference.com/w/cpp/thread/mutex/lock) 加锁，通过 [unlock](https://en.cppreference.com/w/cpp/thread/mutex/unlock) 解锁。一般不手动使用这两个成员函数，而是使用 [std::lock_guard](https://en.cppreference.com/w/cpp/thread/lock_guard) 来自动处理加锁与解锁，它在构造时接受一个 mutex，并会调用 mutex.lock()，析构时会调用 mutex.unlock()

```cpp
#include <iostream>
#include <mutex>

class A {
 public:
  void lock() { std::cout << "lock" << std::endl; }
  void unlock() { std::cout << "unlock" << std::endl; }
};

int main() {
  A a;
  {
    std::lock_guard<A> l(a);  // lock
  }                           // unlock
}
```

* C++17 提供了的 [std::scoped_lock](https://en.cppreference.com/w/cpp/thread/scoped_lock)，它可以接受任意数量的 mutex，并将这些 mutex 传给 [std::lock](https://en.cppreference.com/w/cpp/thread/lock) 来同时上锁，它会对其中一个 mutex 调用 lock()，对其他调用 try_lock()，若 try_lock() 返回 false 则对已经上锁的 mutex 调用 unlock()，然后重新进行下一轮上锁，标准未规定下一轮的上锁顺序，可能不一致，重复此过程直到所有 mutex 上锁，从而达到同时上锁的效果。C++17 支持类模板实参推断，可以省略模板参数

```cpp
#include <iostream>
#include <mutex>

class A {
 public:
  void lock() { std::cout << 1; }
  void unlock() { std::cout << 2; }
  bool try_lock() {
    std::cout << 3;
    return true;
  }
};

class B {
 public:
  void lock() { std::cout << 4; }
  void unlock() { std::cout << 5; }
  bool try_lock() {
    std::cout << 6;
    return true;
  }
};

int main() {
  A a;
  B b;
  {
    std::scoped_lock l(a, b);  // 16
    std::cout << std::endl;
  }  // 25
}
```

* 一般 mutex 和要保护的数据一起放在类中，定义为 private 数据成员，而非全局变量，这样能让代码更清晰。但如果某个成员函数返回指向数据成员的指针或引用，则通过这个指针的访问行为不会被 mutex 限制，因此需要谨慎设置接口，确保 mutex 能锁住数据

```cpp
#include <mutex>

class A {
 public:
  void f() {}
};

class B {
 public:
  A* get_data() {
    std::lock_guard<std::mutex> l(m_);
    return &data_;
  }

 private:
  std::mutex m_;
  A data_;
};

int main() {
  B b;
  A* p = b.get_data();
  p->f();  // 未锁定 mutex 的情况下访问数据
}
```

* 即便在很简单的接口中，也可能遇到 race condition

```cpp
std::stack<int> s；
if (!s.empty()) {
  int n = s.top();  // 此时其他线程 pop 就会获取错误的 top
  s.pop();
}
```

* 上述代码先检查非空再获取栈顶元素，在单线程中是安全的，但在多线程中，检查非空之后，如果其他线程先 pop，就会导致当前线程 top 出错。另一个潜在的竞争是，如果两个线程都未 pop，而是分别获取了 top，虽然不会产生未定义行为，但这种对同一值处理了两次的行为更为严重，因为看起来没有任何错误，很难定位 bug
* 既然如此，为什么不直接让 pop 返回栈顶元素？原因在于，构造返回值的过程可能抛异常，弹出后未返回会导致数据丢失。比如有一个元素为 vector 的 stack，拷贝 vector 需要在堆上分配内存，如果系统负载严重或资源有限（比如 vector 有大量元素），vector 的拷贝构造函数就会抛出 [std::bad_alloc](https://en.cppreference.com/w/cpp/memory/new/bad_alloc) 异常。如果 pop 可以返回栈顶元素值，返回一定是最后执行的语句，stack 在返回前已经弹出了元素，但如果拷贝返回值时抛出异常，就会导致弹出的数据丢失（从栈上移除但拷贝失败）。因此 [std::stack](https://en.cppreference.com/w/cpp/container/stack) 的设计者将这个操作分解为 top 和 pop 两部分
* 下面思考几种把 top 和 pop 合为一步的方法。第一种容易想到的方法是传入一个引用来获取结果值，这种方式的明显缺点是，需要构造一个栈元素类型的实例，这是不现实的，为了获取结果而临时构造一个对象并不划算，元素类型可能不支持赋值（比如用户自定义某个类型），构造函数可能还需要一些参数

```cpp
std::vector<int> res;
s.pop(res);
```

* 因为 pop 返回值时只担心该过程抛异常，第二种方案是为元素类型设置不抛异常的拷贝或移动构造函数，使用 [std::is_nothrow_copy_constructible](https://en.cppreference.com/w/cpp/types/is_copy_constructible) 和 [std::is_nothrow_move_constructible](https://en.cppreference.com/w/cpp/types/is_move_constructible)。但这种方式过于局限，只支持拷贝或移动不抛异常的类型
* 第三种方案是返回指向弹出元素的指针，指针可以自由拷贝且不会抛异常，[std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr) 是个不错的选择，但这个方案的开销太大，尤其是对于内置类型来说，比如 int 为 4 字节， `shared_ptr<int>` 为 16 字节，开销是原来的 4 倍
* 第四种方案是结合方案一二或者一三，比如结合方案一三实现一个线程安全的 stack

```cpp
#include <exception>
#include <memory>
#include <mutex>
#include <stack>
#include <utility>

struct EmptyStack : std::exception {
  const char* what() const noexcept { return "empty stack!"; }
};

template <typename T>
class ConcurrentStack {
 public:
  ConcurrentStack() = default;

  ConcurrentStack(const ConcurrentStack& rhs) {
    std::lock_guard<std::mutex> l(rhs.m_);
    s_ = rhs.s_;
  }

  ConcurrentStack& operator=(const ConcurrentStack&) = delete;

  void push(T x) {
    std::lock_guard<std::mutex> l(m_);
    s_.push(std::move(x));
  }

  bool empty() const {
    std::lock_guard<std::mutex> l(m_);
    return s_.empty();
  }

  std::shared_ptr<T> pop() {
    std::lock_guard<std::mutex> l(m_);
    if (s_.empty()) {
      throw EmptyStack();
    }
    auto res = std::make_shared<T>(std::move(s_.top()));
    s_.pop();
    return res;
  }

  void pop(T& res) {
    std::lock_guard<std::mutex> l(m_);
    if (s_.empty()) {
      throw EmptyStack();
    }
    res = std::move(s_.top());
    s_.pop();
  }

 private:
  mutable std::mutex m_;
  std::stack<T> s_;
};
```

* 之前锁的粒度（锁保护的数据量大小）太小，保护操作覆盖不周全，这里的粒度就较大，覆盖了大量操作。但并非粒度越大越好，如果锁粒度太大，过多线程请求竞争占用资源时，并发的性能就会较差
* 如果给定操作需要对多个 mutex 上锁时，就会引入一个新的潜在问题，即死锁

## 死锁

* 死锁的四个必要条件：互斥、占有且等待、不可抢占、循环等待
* 避免死锁通常建议让两个 mutex 以相同顺序上锁，总是先锁 A 再锁 B，但这并不适用所有情况。[std::lock](https://en.cppreference.com/w/cpp/thread/lock) 可以同时对多个 mutex 上锁，并且没有死锁风险，它可能抛异常，此时就不会上锁，因此要么都锁住，要么都不锁

```cpp
#include <mutex>
#include <thread>

struct A {
  explicit A(int n) : n_(n) {}
  std::mutex m_;
  int n_;
};

void f(A &a, A &b, int n) {
  if (&a == &b) {
    return;  // 防止对同一对象重复加锁
  }
  std::lock(a.m_, b.m_);  // 同时上锁防止死锁
  // 下面按固定顺序加锁，看似不会有死锁的问题
  // 但如果没有 std::lock 同时上锁，另一线程中执行 f(b, a, n)
  // 两个锁的顺序就反了过来，从而可能导致死锁
  std::lock_guard<std::mutex> lock1(a.m_, std::adopt_lock);
  std::lock_guard<std::mutex> lock2(b.m_, std::adopt_lock);

  // 等价实现，先不上锁，后同时上锁
  //   std::unique_lock<std::mutex> lock1(a.m_, std::defer_lock);
  //   std::unique_lock<std::mutex> lock2(b.m_, std::defer_lock);
  //   std::lock(lock1, lock2);

  a.n_ -= n;
  b.n_ += n;
}

int main() {
  A x{70};
  A y{30};

  std::thread t1(f, std::ref(x), std::ref(y), 20);
  std::thread t2(f, std::ref(y), std::ref(x), 10);

  t1.join();
  t2.join();
}
```

* [std::unique_lock](https://en.cppreference.com/w/cpp/thread/unique_lock) 在构造时接受一个 mutex，并会调用 mutex.lock()，析构时会调用 mutex.unlock()

```cpp
#include <iostream>
#include <mutex>

class A {
 public:
  void lock() { std::cout << "lock" << std::endl; }
  void unlock() { std::cout << "unlock" << std::endl; }
};

int main() {
  A a;
  {
    std::unique_lock l(a);  // lock
  }                         // unlock
}
```

* [std::lock_guard](https://en.cppreference.com/w/cpp/thread/lock_guard) 未提供任何接口且不支持拷贝和移动，而 [std::unique_lock](https://en.cppreference.com/w/cpp/thread/unique_lock) 多提供了一些接口，使用更灵活，占用的空间也多一点。一种要求灵活性的情况是转移锁的所有权到另一个作用域

```cpp
std::unique_lock<std::mutex> get_lock() {
  extern std::mutex m;
  std::unique_lock<std::mutex> l(m);
  prepare_data();
  return l;  // 不需要 std::move，编译器负责调用移动构造函数
}

void f() {
  std::unique_lock<std::mutex> l(get_lock());
  do_something();
}
```

* 对一些费时的操作上锁可能造成很多操作被阻塞，可以在面对这些操作时先解锁

```cpp
void process_file_data() {
  std::unique_lock<std::mutex> l(m);
  auto data = get_data();
  l.unlock();  // 费时操作没有必要持有锁，先解锁
  auto res = process(data);
  l.lock();  // 写入数据前上锁
  write_result(data, res);
}
```

* C++17 最优的同时上锁方法是使用 [std::scoped_lock](https://en.cppreference.com/w/cpp/thread/scoped_lock)
* 解决死锁并不简单，[std::lock](https://en.cppreference.com/w/cpp/thread/lock) 和 [std::scoped_lock](https://en.cppreference.com/w/cpp/thread/scoped_lock) 无法获取其中的锁，此时解决死锁更依赖于开发者的能力。避免死锁有四个建议
  * 第一个避免死锁的建议是，一个线程已经获取一个锁时就不要获取第二个。如果每个线程只有一个锁，锁上就不会产生死锁（但除了互斥锁，其他方面也可能造成死锁，比如即使无锁，线程间相互等待也可能造成死锁）
  * 第二个建议是，持有锁时避免调用用户提供的代码。用户提供的代码可能做任何时，包括获取锁，如果持有锁时调用用户代码获取锁，就会违反第一个建议，并造成死锁。但有时调用用户代码是无法避免的
  * 第三个建议是，按固定顺序获取锁。如果必须获取多个锁且不能用 [std::lock](https://en.cppreference.com/w/cpp/thread/lock) 同时获取，最好在每个线程上用固定顺序获取。上面的例子虽然是按固定顺序获取锁，但如果不同时加锁就会出现死锁，对于这种情况的建议是规定固定的调用顺序
  * 第四个建议是使用层级锁，如果一个锁被低层持有，就不允许在高层再上锁
* 层级锁实现如下

```cpp
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
```

### 读写锁（reader-writer mutex）

* 有时会希望对一个数据上锁时，根据情况，对某些操作相当于不上锁，可以并发访问，对某些操作保持上锁，同时最多只允许一个线程访问。比如对于需要经常访问但很少更新的缓存数据，用 [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex) 加锁会导致同时最多只有一个线程可以读数据，这就需要用上读写锁，读写锁允许多个线程并发读但仅一个线程写
* C++14 提供了 [std::shared_timed_mutex](https://en.cppreference.com/w/cpp/thread/shared_timed_mutex)，C++17 提供了接口更少性能更高的 [std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex)，如果多个线程调用 shared_mutex.lock_shared()，多个线程可以同时读，如果此时有一个写线程调用 shared_mutex.lock()，则读线程均会等待该写线程调用 shared_mutex.unlock()。C++11 没有提供读写锁，可使用 [boost::shared_mutex](https://www.boost.org/doc/libs/1_79_0/doc/html/thread/synchronization.html#thread.synchronization.mutex_types.shared_mutex)
* C++14 提供了 [std::shared_lock](https://en.cppreference.com/w/cpp/thread/shared_lock)，它在构造时接受一个 mutex，并会调用 mutex.lock_shared()，析构时会调用 mutex.unlock_shared()

```cpp
#include <iostream>
#include <shared_mutex>

class A {
 public:
  void lock_shared() { std::cout << "lock_shared" << std::endl; }
  void unlock_shared() { std::cout << "unlock_shared" << std::endl; }
};

int main() {
  A a;
  {
    std::shared_lock l(a);  // lock_shared
  }                         // unlock_shared
}
```

* 对于 [std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex)，通常在读线程中用 [std::shared_lock](https://en.cppreference.com/w/cpp/thread/shared_lock) 管理，在写线程中用 [std::unique_lock](https://en.cppreference.com/w/cpp/thread/unique_lock) 管理

```cpp
class A {
 public:
  int read() const {
    std::shared_lock<std::shared_mutex> l(m_);
    return n_;
  }

  int write() {
    std::unique_lock<std::shared_mutex> l(m_);
    return ++n_;
  }

 private:
  mutable std::shared_mutex m_;
  int n_ = 0;
};
```

### 递归锁

* [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex) 是不可重入的，未释放前再次上锁是未定义行为

```cpp
#include <mutex>

class A {
 public:
  void f() {
    m_.lock();
    m_.unlock();
  }

  void g() {
    m_.lock();
    f();
    m_.unlock();
  }

 private:
  std::mutex m_;
};

int main() {
  A{}.g();  // Undefined Behavior
}
```

* 为此 C++ 提供了 [std::recursive_mutex](https://en.cppreference.com/w/cpp/thread/recursive_mutex)，它可以在一个线程上多次获取锁，但在其他线程获取锁之前必须释放所有的锁

```cpp
#include <mutex>

class A {
 public:
  void f() {
    m_.lock();
    m_.unlock();
  }

  void g() {
    m_.lock();
    f();
    m_.unlock();
  }

 private:
  std::recursive_mutex m_;
};

int main() {
  A{}.g();  // OK
}
```

* 多数情况下，如果需要递归锁，说明代码设计存在问题。比如一个类的每个成员函数都会上锁，一个成员函数调用另一个成员函数，就可能多次上锁，这种情况用递归锁就可以避免产生未定义行为。但显然这个设计本身是有问题的，更好的办法是提取其中一个函数作为 private 成员并且不上锁，其他成员先上锁再调用该函数

## 对并发初始化的保护

* 除了对并发访问共享数据的保护，另一种常见的情况是对并发初始化的保护

```cpp
#include <memory>
#include <mutex>
#include <thread>

class A {
 public:
  void f() {}
};

std::shared_ptr<A> p;
std::mutex m;

void init() {
  m.lock();
  if (!p) {
    p.reset(new A);
  }
  m.unlock();
  p->f();
}

int main() {
  std::thread t1{init};
  std::thread t2{init};

  t1.join();
  t2.join();
}
```

* 上锁只是为了保护初始化过程，会不必要地影响性能，一种容易想到的优化方式是双重检查锁模式，但这存在潜在的 race condition

```cpp
#include <memory>
#include <mutex>
#include <thread>

class A {
 public:
  void f() {}
};

std::shared_ptr<A> p;
std::mutex m;

void init() {
  if (!p) {  // 未上锁，其他线程可能在执行 #1，则此时 p 不为空
    std::lock_guard<std::mutex> l(m);
    if (!p) {
      p.reset(new A);  // 1
      // 先分配内存，再在内存上构造 A 的实例并返回内存的指针，最后让 p 指向它
      // 也可能先让 p 指向它，再在内存上构造 A 的实例
    }
  }
  p->f();  // p 可能指向一块还未构造实例的内存，从而崩溃
}

int main() {
  std::thread t1{init};
  std::thread t2{init};

  t1.join();
  t2.join();
}
```

* 为此，C++11 提供了 [std::once_flag](https://en.cppreference.com/w/cpp/thread/once_flag) 和 [std::call_once](https://en.cppreference.com/w/cpp/thread/call_once) 来保证对某个操作只执行一次

```cpp
#include <memory>
#include <mutex>
#include <thread>

class A {
 public:
  void f() {}
};

std::shared_ptr<A> p;
std::once_flag flag;

void init() {
  std::call_once(flag, [&] { p.reset(new A); });
  p->f();
}

int main() {
  std::thread t1{init};
  std::thread t2{init};

  t1.join();
  t2.join();
}
```

* [std::call_once](https://en.cppreference.com/w/cpp/thread/call_once) 也可以用在类中

```cpp
#include <iostream>
#include <mutex>
#include <thread>

class A {
 public:
  void f() {
    std::call_once(flag_, &A::print, this);
    std::cout << 2;
  }

 private:
  void print() { std::cout << 1; }

 private:
  std::once_flag flag_;
};

int main() {
  A a;
  std::thread t1{&A::f, &a};
  std::thread t2{&A::f, &a};
  t1.join();
  t2.join();
}  // 122
```

* static 局部变量在声明后就完成了初始化，这存在潜在的 race condition，如果多线程的控制流同时到达 static 局部变量的声明处，即使变量已在一个线程中初始化，其他线程并不知晓，仍会对其尝试初始化。为此，C++11 规定，如果 static 局部变量正在初始化，线程到达此处时，将等待其完成，从而避免了 race condition。只有一个全局实例时，可以直接用 static 而不需要 [std::call_once](https://en.cppreference.com/w/cpp/thread/call_once)

```cpp
template <typename T>
class Singleton {
 public:
  static T& Instance();
  Singleton(const Singleton&) = delete;
  Singleton& operator=(const Singleton&) = delete;

 private:
  Singleton() = default;
  ~Singleton() = default;
};

template <typename T>
T& Singleton<T>::Instance() {
  static T instance;
  return instance;
}
```
