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