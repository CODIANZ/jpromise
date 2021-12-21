#include <iostream>
#include <jpromise/promise.h>

using namespace JPromise;

void heavy(int x, std::function<void(int)> f) {
  auto t = std::thread([f, x]{
    std::this_thread::sleep_for(std::chrono::milliseconds(x));
    f(x);
  });
  t.detach();
}


int main()
{
  auto p = Promise<int>([](auto resolver){
    resolver.resolve(1);
  })
  .then([](auto x){
      std::cout << x << std::endl;
      return x * 2;
  })
  .then([](auto x){
    std::cout << x << std::endl;
    return Promise<float>([x](auto resolver){
      resolver.resolve(x * 0.1);
    });
  })
  .then([](auto x){
      std::cout << x << std::endl;
      return std::string("ssss");
  });

  p.then([](auto x){
    std::cout << x << std::endl;
  });
}