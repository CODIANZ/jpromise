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

void heavy(std::function<void()> f, int x) {
  auto t = std::thread([f, x]{
    std::this_thread::sleep_for(std::chrono::milliseconds(x));
    f();
  });
  t.detach();
}

template <typename T> typename Promise<T>::sp pvalue(T&& value, int delay = 0) {
  using TT = typename std::remove_const<typename std::remove_reference<T>::type>::type;
  return Promise<>::create<TT>([&value, delay](auto resolver) {
    if(delay == 0) resolver.resolve(std::forward<T>(value));
    else{
      heavy([resolver, value]() {
        resolver.resolve(std::move(value));
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
