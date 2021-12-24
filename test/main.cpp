#include <iostream>
#include <sstream>
#include <array>
#include <jpromise/jpromise.h>

using namespace JPromise;

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
      setTimeout([resolver, _value]() {
        resolver.resolve(std::move(_value));
      }, delay);
    }
  });
}

struct test_error : public std::exception {
  std::string what_;
  test_error(std::string what) noexcept : what_(what) {}
  virtual const char* what() const noexcept { return what_.c_str(); }
};

std::string error_to_string(std::exception_ptr err){
  try{ std::rethrow_exception(err); }
  catch(std::exception& e){
    return e.what();
  }
  catch(...){}
  return "unknown";
}

template <typename T> typename Promise<T>::sp perror(const std::string& text, int delay = 0) {
  auto err = std::make_exception_ptr(test_error{text});
  return Promise<>::create<T>([delay, err](auto resolver)  {
    if(delay == 0) resolver.reject(err);
    else{
      setTimeout([resolver, err]() {
        resolver.reject(err);
      }, delay);
    }
  });
}

auto state_to_string(PromiseState state) {
  switch(state){
    case PromiseState::pending:   return "pending";
    case PromiseState::fulfilled: return "fulfilled";
    case PromiseState::rejected:  return "rejected";
  }
};

void test_1() {
  pvalue(1)
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 1);
    return pvalue(x + 1);
  })
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 2);
    return pvalue(x + 1);
  })
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 3);
    return pvalue(x + 1);
  })
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 4);
    return pvalue(x + 1);
  })
  ->finally([](){
    log() << "finally" << std::endl;
  });
}


void test_2() {
  pvalue(1)
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 1);
    return pvalue<std::string>("a");
  })
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == "a");
    return pvalue(2);
  })
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 2);
    return pvalue<std::string>("b");
  })
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == "b");
    return pvalue(3);
  })
  ->finally([](){
    log() << "finally" << std::endl;
  });
}

void test_3() {
  auto p = pvalue(1)
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 1);
    return x + 1; /* emit number */
  })
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 2);
     /* emit none (= emit x) */
  });

  p->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 2);
  });
}

void test_4() {
  auto p = pvalue(1)
  ->then([](const auto& x){
    log() << x << std::endl;
    assert(x == 1);
    return perror<int>("test4");
  })
  ->then([](const auto& x){
    /* never */
    log() << x << std::endl;
  })
  ->error([](std::exception_ptr e){
    log() << "error " << error_to_string(e) << std::endl;
  });
}


void test_5() {
  {
    log() << "#1 start" << std::endl;
    auto p = pvalue<std::string>("#1 - a", 1000)
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + "b", 1000);
    })
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + "c", 1000);
    })
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + "d", 1000);
    })
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + "e", 1000);
    })
    ->then([](const auto& x){
      log() << x << std::endl;
    })
    ->finally([&](){
      log() << "#1 finally" << std::endl;
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    log() << "wait 2sec" << std::endl;

    log() << "#1 end" << std::endl;
  }

  {
    log() << "#2 start" << std::endl;
    pvalue<std::string>("#2 - a", 1000)
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + "b", 1000);
    })
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + "c", 1000);
    })
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + "d", 1000);
    })
    ->then([](const auto& x){
      log() << x << std::endl;
      return pvalue(x + "e", 1000);
    })
    ->then([](const auto& x){
      log() << x << std::endl;
    })
    ->finally([&](){
      log() << "#2 finally" << std::endl;
    })
    ->wait();
    log() << "#2 end" << std::endl;
  }
}

void test_6() {
  Promise<>::create<int>([](auto resolver){
    throw test_error("#1");
  })
  ->error([](std::exception_ptr err){
    log() << error_to_string(err) << std::endl;
  });

  pvalue(0)
  ->then([](const auto& x){
    throw test_error("#2");
  })
  ->error([](std::exception_ptr err){
    log() << error_to_string(err) << std::endl;
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

void test_8() {
  {
    pvalue<std::string>("#1")
    ->then([](const auto& x){
      log() << x << std::endl;
    })
    ->stand_alone();
  }

  {
    pvalue<std::string>("#2", 1000)
    ->then([](const auto& x){
      log() << x << std::endl;
    })
    ->stand_alone();
  }
  log() << "wait 2 sec" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));

  {
    pvalue<std::string>("#3", 1000)
    ->then([](const auto& x){
      log() << x << std::endl;
    })
    ->stand_alone({
      .on_fulfilled = [](const auto& x){
        std::cout << "on_fulfilled " << x << std::endl;
      }
    });
  }
  log() << "wait 2 sec" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));
}

void test_9() {
  auto p = pvalue<std::string>("hello", 1000);

  auto p1 = p->then([](const auto& x){
    log() << "#1 " << x << std::endl;
  });

  auto p2 = p->then([](const auto& x){
    log() << "#2 " << x << std::endl;
  });

  auto p3 = p->then([](const auto& x){
    log() << "#3 " << x << std::endl;
  });

  p3.reset();

  log() << "wait 2 sec" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));
}

void test_10() {
  {
    auto p1 = pvalue(1, 900);
    auto p2 = pvalue<double>(1.23, 1200);
    auto p3 = pvalue<std::string>("abc", 500);

    auto p = Promise<>::all(p1, p2, p3)
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
    auto p = Promise<>::all<int>(arr.begin(), arr.end())
    ->then([](const auto& x){
      for(auto n : x){
        std::cout << n << std::endl;
      }
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

void test_11() {
  {
    auto p1 = pvalue<std::string>("#1", 1000);
    auto p2 = pvalue<std::string>("#2", 600);
    auto p3 = pvalue<std::string>("#3", 400);

    auto p = Promise<>::race({p1, p2, p3})
    ->then([](const auto& x){
      log() << x << std::endl;  /* x = "#3" */
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
      log() << x << std::endl; /* x = random */
    });

    log() << "wait 2 sec" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

void test_12() {
  auto p1 = pvalue<std::string>("#1", 100);
  auto p2 = pvalue<std::string>("#2", 600);
  auto p3 = pvalue<std::string>("#3", 300);

  for(int i = 0; i < 10; i++){
    auto x =  Promise<>::states(p1, p2, p3)
    ->wait();
    for(auto it = x.begin(); it != x.end(); it++){
      std::cout << state_to_string(*it) << ", ";
    }
    std::cout << std::endl;

    log() << "wait 100 ms" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

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

int main()
{
  log() << "================ test_1 ================" << std::endl;
  test_1();

  log() << "================ test_2 ================" << std::endl;
  test_2();

  log() << "================ test_3 ================" << std::endl;
  test_3();

  log() << "================ test_4 ================" << std::endl;
  test_4();

  log() << "================ test_5 ================" << std::endl;
  test_5();

  log() << "================ test_6 ================" << std::endl;
  test_6();

  log() << "================ test_7 ================" << std::endl;
  test_7();

  log() << "================ test_8 ================" << std::endl;
  test_8();

  log() << "================ test_9 ================" << std::endl;
  test_9();

  log() << "================ test_10 ================" << std::endl;
  test_10();

  log() << "================ test_11 ================" << std::endl;
  test_11();

  log() << "================ test_12 ================" << std::endl;
  test_12();

  log() << "================ test_13 ================" << std::endl;
  test_13();
}
