## [执行策略（execution policy）](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t)

* C++17 对标准库算法重载了并行版本，区别是多了一个指定执行策略的参数

```cpp
std::vector<int> v;
std::sort(std::execution::par, v.begin(), v.end());
```

* [std::execution::par](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag) 表示允许多线程并行执行此算法，注意这是一个权限（permission）而非强制要求（requirement），此算法依然可以被单线程执行
* 另外，如果指定了执行策略，算法复杂度的要求也更宽松，因为并行算法为了利用好系统的并行性通常要做更多工作。比如把工作划分给 100 个处理器，即使总工作是原来的两倍，也仍然能获得原来的五十倍的性能
* [\<execution\>](https://en.cppreference.com/w/cpp/header/execution) 中指定了如下执行策略类

```cpp
std::execution::sequenced_policy
std::execution::parallel_policy
std::execution::parallel_unsequenced_policy
std::execution::unsequenced_policy  // C++20
```

* 并指定了对应的全局对象

```cpp
std::execution::seq
std::execution::par
std::execution::par_unseq
std::execution::unseq  // C++20
```

* 如果使用执行策略，算法的行为就会受执行策略影响，影响方面包括：算法复杂度、抛异常时的行为、算法步骤的执行位置（where）、方式（how）、时刻（when）
* 除了管理并行执行的调度开销，许多并行算法会执行更多的核心操作（交换、比较、使用函数对象等），这样可以减少总的实际消耗时间，从而全面提升性能。这就是算法复杂度受影响的原因，其具体改变因算法不同而异
* 在不指定执行策略时，如下对算法的调用，抛出的异常会被传播

```cpp
std::for_each(v.begin(), v.end(), [](auto x) { throw my_exception(); });
```

* 而指定执行策略时，如果算法执行期间抛出异常，则行为结果由执行策略决定。如果有任何未捕获的异常，执行策略将调用 [std::terminate](https://en.cppreference.com/w/cpp/error/terminate) 终止程序，唯一可能抛出异常的情况是，内部操作不能获取足够的内存资源时抛出 [std::bad_alloc](https://en.cppreference.com/w/cpp/memory/new/bad_alloc)。如下操作将调用 [std::terminate](https://en.cppreference.com/w/cpp/error/terminate) 终止程序

```cpp
std::for_each(std::execution::seq, v.begin(), v.end(),
              [](auto x) { throw my_exception(); });
```

* 不同的执行策略的执行方式也不相同。执行策略会指定执行算法步骤的代理，可以是常规线程、矢量流、GPU 线程或其他任何东西。执行策略也会指定算法步骤运行的顺序限制，比如是否要以特定顺序运行、不同算法步骤的一部分是否可以互相交错或并行运行等。下面对不同的执行策略进行详细解释

### [std::execution::sequenced_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t)

* [std::execution::sequenced_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略要求可以不（may not）并行执行，所有操作将执行在一个线程上。但它也是执行策略，因此与其他执行策略一样会影响算法复杂度和异常行为
* 所有执行在一个线程上的操作必须以某个确定顺序执行，因此这些操作是不能互相交错的。但不规定具体顺序，因此对于不同的函数调用可能产生不同的顺序

```cpp
std::vector<int> v(1000);
int n = 0;
// 把 1-1000 存入容器，存入顺序可能是顺序也可能是乱序
std::for_each(std::execution::seq, v.begin(), v.end(),
              [&](int& x) { x = ++n; });
```

* 因此 [std::execution::sequenced_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略很少要求算法使用迭代器、值、可调用对象，它们可以自由地使用同步机制，可以依赖于同一线程上调用的操作，尽管不能依赖于这些操作的顺序

### [std::execution::parallel_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t)

* [std::execution::parallel_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略提供了基本的跨多个线程的并行执行，操作可以执行在调用算法的线程上，或执行在由库创建的线程上，在一个给定线程上的操作必须以确定顺序执行，并且不能相互交错。同样这个顺序是未指定的，对于不同的调用可能会有不同的顺序。一个给定的操作将在一个固定的线程上运行完整个周期
* 因此 [std::execution::parallel_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略对于迭代器、值、可调用对象的使用就有一定要求，它们在并行调用时不能造成数据竞争，并且不能依赖于统一线程上的其他操作，或者说只能依赖于不运行在同一线程上的其他操作
* 大多数情况都可以使用 [std::execution::parallel_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略

```cpp
std::for_each(std::execution::par, v.begin(), v.end(), [](auto& x) { ++x; });
```

* 只有在元素之间有特定顺序或对共享数据的访问不同步时，它才有问题

```cpp
std::vector<int> v(1000);
int n = 0;
std::for_each(std::execution::par, v.begin(), v.end(), [&](int& x) {
  x = ++n;
});  // 如果多个线程执行 lambda 就会对 n 产生数据竞争
```

* 因此使用 [std::execution::parallel_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略时，应该事先考虑可能出现的未定义行为。可以用 mutex 或原子变量来解决竞争问题，但这就影响了并发性。不过这个例子只是为了阐述此情况，一般使用 [std::execution::parallel_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略时都是允许同步访问共享数据的

### [std::execution::parallel_unsequenced_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t)

* [std::execution::parallel_unsequenced_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略提供了最大可能的并行化，代价是对算法使用的迭代器、值和可调用对象有最严格的的要求
* 使用 [std::execution::parallel_unsequenced_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略的算法允许以无序的方式在任意未指定的线程中执行，并且在每个线程中彼此不排序。也就是说，操作可以在单个线程上互相交错，同一线程上的第二个操作可以开始于第一个操作结束前，并且可以在线程间迁移，一个给定的操作可以开始于一个线程，运行于另一线程，而完成于第三个线程
* 使用 [std::execution::parallel_unsequenced_policy](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) 策略时，提供给算法的迭代器、值、可调用对象上的操作不能使用任何形式的同步，也不能调用与其他代码同步的任何函数。这意味着操作只能作用于相关元素，或任何基于这些元素的可访问数据，并且不能修改任何线程间或元素间的共享数据

## 标准库并行算法

* [\<algorithm\>](https://en.cppreference.com/w/cpp/algorithm) 和 [\<numberic\>](https://en.cppreference.com/w/cpp/header/numeric) 中的大部分算法都重载了并行版本。[std::accumlate](https://en.cppreference.com/w/cpp/algorithm/accumulate) 没有并行版本，但 C++17 提供了 [std::reduce](https://en.cppreference.com/w/cpp/algorithm/reduce)

```cpp
std::accumulate(v.begin(), v.end(), 0);
std::reduce(std::execution::par, v.begin(), v.end());
```

* 如果常规算法有并行版的重载，则并行版对常规算法原有的所有重载都有一个对应重载版本

```cpp
template <class RandomIt>
void sort(RandomIt first, RandomIt last);

template <class RandomIt, class Compare>
void sort(RandomIt first, RandomIt last, Compare comp);

// 并行版对应有两个重载
template <class ExecutionPolicy, class RandomIt>
void sort(ExecutionPolicy&& policy, RandomIt first, RandomIt last);

template <class ExecutionPolicy, class RandomIt, class Compare>
void sort(ExecutionPolicy&& policy, RandomIt first, RandomIt last,
          Compare comp);
```

* 但并行版的重载对部分算法有一些区别，如果常规版本使用的是输入迭代器（input iterator）或输出迭代器（output iterator），则并行版的重载将使用前向迭代器（forward iterator）

```cpp
template <class InputIt, class OutputIt>
OutputIt copy(InputIt first, InputIt last, OutputIt d_first);

template <class ExecutionPolicy, class ForwardIt1, class ForwardIt2>
ForwardIt2 copy(ExecutionPolicy&& policy, ForwardIt1 first, ForwardIt1 last,
                ForwardIt2 d_first);
```

* 输入迭代器只能用来读取指向的值，迭代器自增后就再也无法访问之前指向的值，它一般用于从控制台或网络输入，或生成序列，比如 [std::istream_iterator](https://en.cppreference.com/w/cpp/iterator/istream_iterator)。同理，输出迭代器一般用来输出到文件，或添加值到容器，也是单向的，比如 [std::ostream_iterator](https://en.cppreference.com/w/cpp/iterator/ostream_iterator)
* 前向迭代器返回元素的引用，因此可以用于读写，它同样只能单向传递，[std::forward_list](https://en.cppreference.com/w/cpp/container/forward_list) 的迭代器就是前向迭代器，虽然它不可以回到之前指向的值，但可以存储一个指向之前元素的拷贝（比如 [std::forward_list::begin](https://en.cppreference.com/w/cpp/container/forward_list/begin)）来重复利用。对于并行性来说，可以重复利用迭代器很重要。此外，前向迭代器的自增不会使其他的迭代器拷贝失效，这样就不用担心其他线程中的迭代器受影响。如果使用输入迭代器，所有线程只能共用一个迭代器，显然无法并行
* [std::execution::par](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag) 是最常用的策略，除非实现提供了更符合需求的非标准策略。一些情况下也可以使用 [std::execution::par_unseq](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag)，虽然这不保证更好的并发性，但它给了库通过重排和交错任务来提升性能的可能性，不过代价就是不能使用同步机制，要确保线程安全只能让算法本身不会让多个线程访问同一元素，并在调用该算法的外部使用同步机制来避免其他线程对数据的访问
* 内部带同步机制只能使用 [std::execution::par](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag)，如果使用 [std::execution::par_unseq](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag) 会出现未定义行为

```cpp
#include <algorithm>
#include <mutex>
#include <vector>

class A {
 public:
  int get() const {
    std::lock_guard<std::mutex> l(m_);
    return n_;
  }

  void inc() {
    std::lock_guard<std::mutex> l(m_);
    ++n_;
  }

 private:
  mutable std::mutex m_;
  int n_ = 0;
};

void f(std::vector<A>& v) {
  std::for_each(std::execution::par, v.begin(), v.end(), [](A& x) { x.inc(); });
}
```

* 如果使用 [std::execution::par_unseq](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag) 则应该在外部使用同步机制

```cpp
#include <algorithm>
#include <mutex>
#include <vector>

class A {
 public:
  int get() const { return n_; }
  void inc() { ++n_; }

 private:
  int n_ = 0;
};

class B {
 public:
  void lock() { m_.lock(); }
  void unlock() { m_.unlock(); }
  std::vector<A>& get() { return v_; }

 private:
  std::mutex m_;
  std::vector<A> v_;
};

void f(B& x) {
  std::lock_guard<std::mutex> l(x);
  auto& v = x.get();
  std::for_each(std::execution::par_unseq, v.begin(), v.end(),
                [](A& x) { x.inc(); });
}
```

* 下面是一个更实际的例子。假如有一个网站，访问日志有上百万条，为了方便查看数据需要对日志进行处理。对日志每行的处理是独立的工作，很适合使用并行算法

```cpp
struct Log {
  std::string page;
  time_t visit_time;
  // any other fields
};

extern Log parse(const std::string& line);

using Map = std::unordered_map<std::string, unsigned long long>;

Map f(const std::vector<std::string>& v) {
  struct Combine {
    // log、Map 两个参数有四种组合，所以需要四个重载
    Map operator()(Map lhs, Map rhs) const {
      if (lhs.size() < rhs.size()) {
        std::swap(lhs, rhs);
      }
      for (const auto& x : rhs) {
        lhs[x.first] += x.second;
      }
      return lhs;
    }

    Map operator()(Log l, Map m) const {
      ++m[l.page];
      return m;
    }

    Map operator()(Map m, Log l) const {
      ++m[l.page];
      return m;
    }

    Map operator()(Log lhs, Log rhs) const {
      Map m;
      ++m[lhs.page];
      ++m[rhs.page];
      return m;
    }
  };

  return std::transform_reduce(std::execution::par, v.begin(), v.end(),
                               Map{},      // 初始值，一个空的 map
                               Combine{},  // 结合两个元素的二元操作
                               parse);  // 对每个元素执行的一元操作
}
```
