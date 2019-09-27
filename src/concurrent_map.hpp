#pragma once

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
