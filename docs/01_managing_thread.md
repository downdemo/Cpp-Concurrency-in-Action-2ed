## [std::thread](https://en.cppreference.com/w/cpp/thread/thread)

* 每个程序有一个执行 main() 函数的主线程，将函数添加为 [std::thread](https://en.cppreference.com/w/cpp/thread/thread) 的参数即可启动另一个线程，两个线程会同时运行。

```cpp
#include <iostream>
#include <thread>

void f() { std::cout << "hello world"; }

int main() {
  std::thread t{f};
  t.join();  // 等待新起的线程退出
}
```

* [std::thread](https://en.cppreference.com/w/cpp/thread/thread) 的参数也可以是函数对象或者 lambda

```cpp
#include <iostream>
#include <thread>

struct A {
  void operator()() const { std::cout << 1; }
};

int main() {
  A a;
  std::thread t1(a);  // 会调用 A 的拷贝构造函数
  std::thread t2(A());  // most vexing parse，声明名为 t2 参数类型为 A 的函数
  std::thread t3{A()};
  std::thread t4((A()));
  std::thread t5{[] { std::cout << 1; }};
  t1.join();
  t3.join();
  t4.join();
  t5.join();
}
```

* 在线程销毁前要对其调用 [join](https://en.cppreference.com/w/cpp/thread/thread/join) 等待线程退出或 [detach](https://en.cppreference.com/w/cpp/thread/thread/detach) 将线程分离，否则 [std::thread](https://en.cppreference.com/w/cpp/thread/thread) 的析构函数会调用 [std::terminate](https://en.cppreference.com/w/cpp/error/terminate) 终止程序，注意分离线程可能出现空悬引用的隐患

```cpp
#include <iostream>
#include <thread>

class A {
 public:
  A(int& x) : x_(x) {}

  void operator()() const {
    for (int i = 0; i < 1000000; ++i) {
      call(x_);  // 存在对象析构后引用空悬的隐患
    }
  }

 private:
  void call(int& x) {}

 private:
  int& x_;
};

void f() {
  int x = 0;
  A a{x};
  std::thread t{a};
  t.detach();  // 不等待 t 结束
}  // 函数结束后 t 可能还在运行，而 x 已经销毁，a.x_ 为空悬引用

int main() {
  std::thread t{f};  // 导致空悬引用
  t.join();
}
```

* [join](https://en.cppreference.com/w/cpp/thread/thread/join) 会在线程结束后清理 [std::thread](https://en.cppreference.com/w/cpp/thread/thread)，使其与完成的线程不再关联，因此对一个线程只能进行一次 [join](https://en.cppreference.com/w/cpp/thread/thread/join)

```cpp
#include <thread>

int main() {
  std::thread t([] {});
  t.join();
  t.join();  // 错误
}
```

* 如果线程运行过程中发生异常，之后的 [join](https://en.cppreference.com/w/cpp/thread/thread/join) 会被忽略，为此需要捕获异常，并在抛出异常前 [join](https://en.cppreference.com/w/cpp/thread/thread/join)

```cpp
#include <thread>

int main() {
  std::thread t([] {});
  try {
    throw 0;
  } catch (int x) {
    t.join();  // 处理异常前先 join()
    throw x;   // 再将异常抛出
  }
  t.join();  // 之前抛异常，不会执行到此处
}
```

* C++20 提供了 [std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)，它会在析构函数中对线程 [join](https://en.cppreference.com/w/cpp/thread/thread/join)

```cpp
#include <thread>

int main() {
  std::jthread t([] {});
}
```

* [detach](https://en.cppreference.com/w/cpp/thread/thread/detach) 分离线程会让线程在后台运行，一般将这种在后台运行的线程称为守护线程，守护线程与主线程无法直接交互，也不能被 [join](https://en.cppreference.com/w/cpp/thread/thread/join)

```cpp
std::thread t([] {});
t.detach();
assert(!t.joinable());
```

* 创建守护线程一般是为了长时间运行，比如有一个文档处理应用，为了同时编辑多个文档，每次新开一个文档，就可以开一个对应的守护线程

```cpp
void edit_document(const std::string& filename) {
  open_document_and_display_gui(filename);
  while (!done_editing()) {
    user_command cmd = get_user_input();
    if (cmd.type == open_new_document) {
      const std::string new_name = get_filename_from_user();
      std::thread t(edit_document, new_name);
      t.detach();
    } else {
      process_user_input(cmd);
    }
  }
}
```

## 为带参数的函数创建线程

* 有参数的函数也能传给 [std::thread](https://en.cppreference.com/w/cpp/thread/thread)，参数的默认实参会被忽略

```cpp
#include <thread>

void f(int i = 1) {}

int main() {
  std::thread t{f, 42};  // std::thread t{f} 则会出错，因为默认实参会被忽略
  t.join();
}
```

* 参数的引用类型也会被忽略，为此要使用 [std::ref](https://en.cppreference.com/w/cpp/utility/functional/ref)

```cpp
#include <cassert>
#include <thread>

void f(int& i) { ++i; }

int main() {
  int i = 1;
  std::thread t{f, std::ref(i)};
  t.join();
  assert(i == 2);
}
```

* 如果对一个实例的 non-static 成员函数创建线程，第一个参数类型为成员函数指针，第二个参数类型为实例指针，后续参数为函数参数

```cpp
#include <iostream>
#include <thread>

class A {
 public:
  void f(int i) { std::cout << i; }
};

int main() {
  A a;
  std::thread t1{&A::f, &a, 42};  // 调用 a->f(42)
  std::thread t2{&A::f, a, 42};   // 拷贝构造 tmp_a，再调用 tmp_a.f(42)
  t1.join();
  t2.join();
}
```

* 如果要为参数是 move-only 类型的函数创建线程，则需要使用 [std::move](https://en.cppreference.com/w/cpp/utility/move) 传入参数

```cpp
#include <iostream>
#include <thread>
#include <utility>

void f(std::unique_ptr<int> p) { std::cout << *p; }

int main() {
  std::unique_ptr<int> p(new int(42));
  std::thread t{f, std::move(p)};
  t.join();
}
```

## 转移线程所有权

* [std::thread](https://en.cppreference.com/w/cpp/thread/thread) 是 move-only 类型，不能拷贝，只能通过移动转移所有权，但不能转移所有权到 joinable 的线程 

```cpp
#include <thread>
#include <utility>

void f() {}
void g() {}

int main() {
  std::thread a{f};
  std::thread b = std::move(a);
  assert(!a.joinable());
  assert(b.joinable());
  a = std::thread{g};
  assert(a.joinable());
  assert(b.joinable());
  // a = std::move(b);  // 错误，不能转移所有权到 joinable 的线程
  a.join();
  a = std::move(b);
  assert(a.joinable());
  assert(!b.joinable());
  a.join();
}
```

* 移动操作同样适用于支持移动的容器

```cpp
#include <algorithm>
#include <thread>
#include <vector>

int main() {
  std::vector<std::thread> v;
  for (int i = 0; i < 10; ++i) {
    v.emplace_back([] {});
  }
  std::for_each(std::begin(v), std::end(v), std::mem_fn(&std::thread::join));
}
```

* [std::thread](https://en.cppreference.com/w/cpp/thread/thread) 可以作为函数返回值

```cpp
#include <thread>

std::thread f() {
  return std::thread{[] {}};
}

int main() {
  std::thread t{f()};
  t.join();
}
```

* [std::thread](https://en.cppreference.com/w/cpp/thread/thread) 也可以作为函数参数

```cpp
#include <thread>
#include <utility>

void f(std::thread t) { t.join(); }

int main() {
  f(std::thread([] {}));
  std::thread t([] {});
  f(std::move(t));
}
```

* 实现一个可以直接用 [std::thread](https://en.cppreference.com/w/cpp/thread/thread) 构造的自动清理线程的类

```cpp
#include <stdexcept>
#include <thread>
#include <utility>

class scoped_thread {
 public:
  explicit scoped_thread(std::thread x) : t_(std::move(x)) {
    if (!t_.joinable()) {
      throw std::logic_error("no thread");
    }
  }
  ~scoped_thread() { t_.join(); }
  scoped_thread(const scoped_thread&) = delete;
  scoped_thread& operator=(const scoped_thread&) = delete;

 private:
  std::thread t_;
};

int main() {
  scoped_thread t{std::thread{[] {}}};
}
```

* 类似 [std::jthread](https://en.cppreference.com/w/cpp/thread/jthread) 的类

```cpp
#include <thread>

class Jthread {
 public:
  Jthread() noexcept = default;

  template <typename T, typename... Ts>
  explicit Jthread(T&& f, Ts&&... args)
      : t_(std::forward<T>(f), std::forward<Ts>(args)...) {}

  explicit Jthread(std::thread x) noexcept : t_(std::move(x)) {}
  Jthread(Jthread&& rhs) noexcept : t_(std::move(rhs.t_)) {}

  Jthread& operator=(Jthread&& rhs) noexcept {
    if (joinable()) {
      join();
    }
    t_ = std::move(rhs.t_);
    return *this;
  }

  Jthread& operator=(std::thread t) noexcept {
    if (joinable()) {
      join();
    }
    t_ = std::move(t);
    return *this;
  }

  ~Jthread() noexcept {
    if (joinable()) {
      join();
    }
  }

  void swap(Jthread&& rhs) noexcept { t_.swap(rhs.t_); }
  std::thread::id get_id() const noexcept { return t_.get_id(); }
  bool joinable() const noexcept { return t_.joinable(); }
  void join() { t_.join(); }
  void detach() { t_.detach(); }
  std::thread& as_thread() noexcept { return t_; }
  const std::thread& as_thread() const noexcept { return t_; }

 private:
  std::thread t_;
};

int main() {
  Jthread t{[] {}};
}
```

## 查看硬件支持的线程数量

* [hardware_concurrency](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency) 会返回硬件支持的并发线程数

```cpp
#include <iostream>
#include <thread>

int main() {
  unsigned int n = std::thread::hardware_concurrency();
  std::cout << n << " concurrent threads are supported.\n";
}
```

* 并行版本的 [std::accumulate](https://en.cppreference.com/w/cpp/algorithm/accumulate)

```cpp
#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <numeric>
#include <thread>
#include <vector>

template <typename Iterator, typename T>
struct accumulate_block {
  void operator()(Iterator first, Iterator last, T& res) {
    res = std::accumulate(first, last, res);
  }
};

template <typename Iterator, typename T>
T parallel_accumulate(Iterator first, Iterator last, T init) {
  long len = std::distance(first, last);
  if (!len) {
    return init;
  }
  long min_per_thread = 25;
  long max_threads = (len + min_per_thread - 1) / min_per_thread;
  long hardware_threads = std::thread::hardware_concurrency();
  long num_threads =  // 线程数量
      std::min(hardware_threads == 0 ? 2 : hardware_threads, max_threads);
  long block_size = len / num_threads;  // 每个线程中的数据量
  std::vector<T> res(num_threads);
  std::vector<std::thread> threads(num_threads - 1);  // 已有主线程故少一个线程
  Iterator block_start = first;
  for (long i = 0; i < num_threads - 1; ++i) {
    Iterator block_end = block_start;
    std::advance(block_end, block_size);  // block_end 指向当前块尾部
    threads[i] = std::thread{accumulate_block<Iterator, T>{}, block_start,
                             block_end, std::ref(res[i])};
    block_start = block_end;
  }
  accumulate_block<Iterator, T>()(block_start, last, res[num_threads - 1]);
  std::for_each(threads.begin(), threads.end(),
                std::mem_fn(&std::thread::join));
  return std::accumulate(res.begin(), res.end(), init);
}

int main() {
  std::vector<long> v(1000000);
  std::iota(std::begin(v), std::end(v), 0);
  long res = std::accumulate(std::begin(v), std::end(v), 0);
  assert(parallel_accumulate(std::begin(v), std::end(v), 0) == res);
}
```

## 线程号

* 可以通过对线程实例调用成员函数 [get_id](https://en.cppreference.com/w/cpp/thread/thread/get_id) 或在当前线程中调用 [std::this_thread::get_id](https://en.cppreference.com/w/cpp/thread/get_id) 获取 [线程号](https://en.cppreference.com/w/cpp/thread/thread/id)，其本质是一个无符号整型的封装，允许拷贝和比较，因此可以将其作为容器的键值，如果两个线程的线程号相等，则两者是同一线程或都是空线程（一般空线程的线程号为 0）

```cpp
std::thread::id master_thread_id;

void f() {
  if (std::this_thread::get_id() == master_thread_id) {
    do_master_thread_work();
  }
  do_common_work();
}
```

## CPU 亲和性（affinity）

* 一种性能优化的方法是，将线程绑定到一个指定的 CPU core 上运行，以避免多核 CPU 上下文切换和 cache miss 的开销，Linux 中实现如下

```cpp
#include <pthread.h>
#include <sched.h>
#include <string.h>

#include <iostream>
#include <thread>

void affinity_cpu(std::thread::native_handle_type t, int cpu_id) {
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  CPU_SET(cpu_id, &cpu_set);
  int res = pthread_setaffinity_np(t, sizeof(cpu_set), &cpu_set);
  if (res != 0) {
    errno = res;
    std::cout << "fail to affinity" << strerror(errno) << std::endl;
  }
}

void f() {}

int main() {
  int cpu_id = 0;
  std::thread t{f};
  affinity_cpu(t.native_handle(), cpu_id);
  t.join();
}
```
