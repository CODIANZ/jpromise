#include <iostream>
#include <jpromise/promise.h>

using namespace JPromise;

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

struct test_error : std::exception {};

template <typename T> typename Promise<T>::sp perror(int delay = 0) {
  return Promise<>::create<T>([delay](auto resolver)  {
    if(delay == 0) resolver.reject(std::make_exception_ptr(test_error{}));
    else{
      heavy([resolver]() {
        resolver.reject(std::make_exception_ptr(test_error{}));
      }, delay);
    }
  });
}

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
    return perror<int>();
  })
  ->then([](const auto& x){
    /* never */
    log() << x << std::endl;
  })
  ->error([](std::exception_ptr){
    log() << "error" << std::endl;
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
    std::condition_variable cond;
    auto p = pvalue<std::string>("#2 - a", 1000)
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
      cond.notify_one();
    });
    std::mutex mtx;
    std::unique_lock<std::mutex> lock(mtx);
    cond.wait(lock);
    log() << "#2 end" << std::endl;
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
}