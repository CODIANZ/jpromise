# jpromise

Javascript like promise for C++.

## usage

### create Promise

#### Promise<>::create<T>

```cpp
  auto p = Promise<>::create<int>([](auto resolver) {
    resolver.resolve(1);
  });
```

```cpp
  auto p = Promise<>::create<int>([](auto resolver) {
    resolver.reject(exception);
  });
```

#### Promise<>::resolve<T>

```cpp
  auto p = Promise<>::resolve(1);
```

#### Promise<>::reject<>

```cpp
  auto p = Promise<>::reject<>(exception);
```

### promise chaining

```cpp
  Promise<>::resolve(1)
  ->then([](int x){ /* x = 1 */
    return x + 1;
  })
  ->then([](int x){ /* x = 2 */
    return Promise<>::resolve<std::string>("abc");
  })
  ->then([](std::string x){ /* x = "abc" */
    return x + "def";
  })
  ->then([](std::string x){ /* x = "abcdef" */
    /* returns void */
  })
  ->then([](std::string x){  /* x = "abcdef" */
  });
```

```cpp
  Promise<>::resolve(1)
  ->then([](int x){ /* x = 1 */
    return Promise<>::create<int>([](auto resolver) {
      resolver.reject(exception);
    });
  })
  ->then([](int){
    /* never */
  })
  ->error([](std::exception_ptr){
    /* come here */
  });
```

#### complex nesting

```cpp
std::ostream& log() {
  return std::cout << std::this_thread::get_id() << " : ";
}

void setTimeout(std::function<void()> f, int x) {
  auto t = std::thread([f, x]{
    std::this_thread::sleep_for(std::chrono::milliseconds(x));
    f();
  });
  t.detach();
}

template <typename T, typename TT = typename std::remove_const<typename std::remove_reference<T>::type>::type>
auto pvalue(T&& value, int delay = 0) -> typename Promise<TT>::sp {
  auto _value = std::forward<T>(value);
  return Promise<>::create<TT>([&_value, delay](auto resolver) {
    if(delay == 0) resolver.resolve(_value);
    else{
      heavy([resolver, _value]() {
        resolver.resolve(std::move(_value));
      }, delay);
    }
  });
}

void test_7() {
  const auto r = pvalue(1, 100)
  ->then([](const auto& x){
    log() << x << std::endl;
    return pvalue(x + 1, 100)
    ->then([](const auto& x){
      log() << x << std::endl;
      return x + 1;
    })
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + 1, 100)
      ->then([](const auto& x){
        log() << x << std::endl;
        return pvalue(x + 1, 100);
      });
    });
  })
  ->then([](const auto& x){
    log() << x << std::endl;
    std::stringstream ss;
    ss << "result = " << x << std::endl;
    return ss.str();
  })
  ->wait();
  log() << r << std::endl;
}
```

```sh
0x70000f717000 : 1
0x70000f79a000 : 2
0x70000f79a000 : 3
0x70000f81d000 : 4
0x70000f717000 : 5
0x11ccfae00 : result = 5
```


#### Promise.all()

```cpp
void test_10() {
  {
    auto p1 = pvalue(1, 900);
    auto p2 = pvalue<double>(1.23, 1200);
    auto p3 = pvalue<std::string>("abc", 500);

    auto p = Promise<>::all_any(p1, p2, p3)
    ->then([](const auto& x){ /* x = std::tuple<int, double, std::string> */
      std::cout << std::get<0>(x) << std::endl; /** int 1 */
      std::cout << std::get<1>(x) << std::endl; /** double 1.23 */
      std::cout << std::get<2>(x) << std::endl; /** string "abc" */
      assert(std::get<0>(x) == 1);
      assert(std::get<1>(x) == 1.23);
      assert(std::get<2>(x) == "abc");
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  {
    auto p1 = pvalue(1, 1000);
    auto p2 = pvalue(2, 600);
    auto p3 = pvalue(3, 400);

    auto p = Promise<>::all({p1, p2, p3})
    ->then([](const auto& x){
      for(auto n : x){
        std::cout << n << std::endl;
      }
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }

  {
    std::array<Promise<int>::sp, 10> arr;
    for(int i = 0; i < arr.size(); i++){
      arr[i] = pvalue(i, 1000);
    }
    auto p = Promise<>::all(arr.begin(), arr.end())
    ->then([](const auto& x){
      for(auto n : x){
        std::cout << n << std::endl;
      }
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}
```

#### Promise.race()

```cpp
void test_11() {
  {
    auto p1 = pvalue<std::string>("#1", 1000);
    auto p2 = pvalue<std::string>("#2", 600);
    auto p3 = pvalue<std::string>("#3", 400);

    auto p = Promise<>::race({p1, p2, p3})
    ->then([](const auto& x){
      std::cout << x << std::endl;  /* x = "#3" */
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }

  {
    std::array<Promise<int>::sp, 10> arr;
    for(int i = 0; i < arr.size(); i++){
      arr[i] = pvalue(i, 1000);
    }
    auto p = Promise<>::race<int>(arr.begin(), arr.end())
    ->then([](const auto& x){
      std::cout << x << std::endl; /* x = random */
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}
```

#### Promise.allSettled()

```cpp
void test_13() {
  {
    std::array<Promise<int>::sp, 10> arr;
    for(int i = 0; i < arr.size(); i++){
      arr[i] = i % 3 == 0 ? pvalue(i, 1000) : perror<int>("error", i * 100);
    }

    std::stringstream ss;
    auto p = Promise<>::all_settled(arr.begin(), arr.end())
    ->then([&](const auto& x){
      for(auto it = x.begin(); it != x.end(); it++){
        ss << state_to_string(*it) << ", ";
      }
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    log() << ss.str() << std::endl;
  }
  {
    auto p1 = pvalue<std::string>("abc", 100);
    auto p2 = pvalue(1, 600);
    auto p3 = pvalue(1.23, 300);
    auto p4 = perror<int>("error", 200);
    auto p5 = pvalue(true, 800);

    std::stringstream ss;
    auto p = Promise<>::all_settled_any(p1, p2, p3, p4, p5)
    ->then([&](const auto& x){
      for(auto it = x.begin(); it != x.end(); it++){
        ss << state_to_string(*it) << ", ";
      }
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    log() << ss.str();
  }
}
```
