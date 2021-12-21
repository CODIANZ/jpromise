#include <iostream>
#include <jpromise/promise2.h>

using namespace JPromise2;

void heavy(std::function<void()> f, int x) {
  auto t = std::thread([f, x]{
    std::this_thread::sleep_for(std::chrono::milliseconds(x));
    f();
  });
  t.detach();
}

std::ostream& log() {
  return std::cout << std::this_thread::get_id() << " : ";
}

int main()
{
  log() << "start" << std::endl;
  Promise<>::create<int>([](auto resolver){
    heavy([resolver]() mutable{
      resolver.resolve(1);
    }, 500);
  })
  ->then([](auto x){
    log() << x << std::endl;
    return Promise<>::create<int>([x](auto resolver){
      heavy([resolver, x]() mutable{
        resolver.resolve(x * 2);
      }, 500);
    });
  })
  ->then([](auto x){
    log() << x << std::endl;
    return std::string("abc");
  })
  ->then([](auto x){
    log() << x << std::endl;
  });
  log() << "finish" << std::endl;
}